/*
 * Copyright 2026, Koan contributors. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include "KmsGles.h"

#include <new>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <gbm.h>


#define TRACE(x...) fprintf(stderr, "kms gles: " x)

#ifndef GL_UNPACK_ROW_LENGTH_EXT
#	define GL_UNPACK_ROW_LENGTH_EXT 0x0CF2
#endif
#ifndef EGL_BUFFER_AGE_EXT
#	define EGL_BUFFER_AGE_EXT 0x313D
#endif


static const int32 kDamageHistory = 4;


struct KmsGlTarget {
	gbm_surface*		surface;
	EGLSurface			eglSurface;
	GLuint				texture;
	uint32				width;
	uint32				height;
	uint32				regionWidth;
	uint32				regionHeight;
	KmsRect				destination;
	// what each of the last frames changed, in surface pixels
	KmsRect				history[kDamageHistory];
	int32				frames;
};


static const char* kVertexShader =
	"attribute vec2 position;\n"
	"varying vec2 coordinate;\n"
	"void main() {\n"
	"	coordinate = vec2(position.x, 1.0 - position.y);\n"
	"	gl_Position = vec4(position * 2.0 - 1.0, 0.0, 1.0);\n"
	"}\n";

// B_RGB32 is B, G, R, X in memory: uploaded as RGBA, swizzled here
static const char* kFragmentShader =
	"precision mediump float;\n"
	"varying vec2 coordinate;\n"
	"uniform sampler2D source;\n"
	"void main() {\n"
	"	gl_FragColor = vec4(texture2D(source, coordinate).bgr, 1.0);\n"
	"}\n";


static GLuint
compile_shader(GLenum type, const char* text)
{
	GLuint shader = glCreateShader(type);
	glShaderSource(shader, 1, &text, NULL);
	glCompileShader(shader);
	GLint ok = 0;
	glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
	if (!ok) {
		char log[512] = {};
		glGetShaderInfoLog(shader, sizeof(log) - 1, NULL, log);
		TRACE("shader: %s\n", log);
		glDeleteShader(shader);
		return 0;
	}
	return shader;
}


static bool
has_extension(const char* list, const char* name)
{
	if (list == NULL)
		return false;
	size_t length = strlen(name);
	for (const char* found = strstr(list, name); found != NULL;
			found = strstr(found + 1, name)) {
		if ((found == list || found[-1] == ' ')
			&& (found[length] == ' ' || found[length] == '\0'))
			return true;
	}
	return false;
}


KmsGles::KmsGles()
	:
	fDisplay(NULL),
	fConfig(NULL),
	fContext(NULL),
	fDevice(NULL),
	fProgram(0),
	fPositionLocation(-1),
	fTextureLocation(-1),
	fUnpackSubimage(false),
	fBufferAge(false),
	fSoftware(false),
	fMaxTextureSize(0)
{
	fRenderer[0] = '\0';
}


KmsGles::~KmsGles()
{
	Uninit();
}


status_t
KmsGles::Init(gbm_device* device)
{
	PFNEGLGETPLATFORMDISPLAYEXTPROC getPlatformDisplay
		= (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress(
			"eglGetPlatformDisplayEXT");
	EGLDisplay display = getPlatformDisplay != NULL
		? getPlatformDisplay(EGL_PLATFORM_GBM_KHR, device, NULL)
		: eglGetDisplay((EGLNativeDisplayType)device);
	if (display == EGL_NO_DISPLAY)
		return B_NOT_SUPPORTED;
	EGLint major, minor;
	if (!eglInitialize(display, &major, &minor)) {
		TRACE("eglInitialize failed: 0x%x\n", eglGetError());
		return B_NOT_SUPPORTED;
	}
	if (!eglBindAPI(EGL_OPENGL_ES_API)) {
		eglTerminate(display);
		return B_NOT_SUPPORTED;
	}

	static const EGLint kAttributes[] = {
		EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
		EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8,
		EGL_ALPHA_SIZE, 0,
		EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
		EGL_NONE
	};
	EGLint count = 0;
	eglChooseConfig(display, kAttributes, NULL, 0, &count);
	EGLConfig* configs = count > 0
		? (EGLConfig*)malloc(sizeof(EGLConfig) * count) : NULL;
	EGLConfig chosen = NULL;
	if (configs != NULL
		&& eglChooseConfig(display, kAttributes, configs, count, &count)) {
		// the one the surfaces' format is
		for (EGLint i = 0; i < count && chosen == NULL; i++) {
			EGLint visual = 0;
			if (eglGetConfigAttrib(display, configs[i], EGL_NATIVE_VISUAL_ID,
					&visual) && (uint32)visual == GBM_FORMAT_XRGB8888)
				chosen = configs[i];
		}
	}
	free(configs);
	if (chosen == NULL) {
		TRACE("no XRGB8888 config\n");
		eglTerminate(display);
		return B_NOT_SUPPORTED;
	}

	static const EGLint kContext[] = {
		EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE
	};
	EGLContext context = eglCreateContext(display, chosen, EGL_NO_CONTEXT,
		kContext);
	if (context == EGL_NO_CONTEXT) {
		eglTerminate(display);
		return B_NOT_SUPPORTED;
	}
	const char* extensions = eglQueryString(display, EGL_EXTENSIONS);
	bool surfaceless = has_extension(extensions,
		"EGL_KHR_surfaceless_context");
	if (!surfaceless || !eglMakeCurrent(display, EGL_NO_SURFACE,
			EGL_NO_SURFACE, context)) {
		// the program is made with the first surface current
		surfaceless = false;
	}

	fDisplay = display;
	fConfig = chosen;
	fContext = context;
	fDevice = device;
	fBufferAge = has_extension(extensions, "EGL_EXT_buffer_age");
	if (surfaceless) {
		const char* renderer = (const char*)glGetString(GL_RENDERER);
		strlcpy(fRenderer, renderer != NULL ? renderer : "?",
			sizeof(fRenderer));
	}
	return B_OK;
}


void
KmsGles::Uninit()
{
	if (fDisplay == NULL)
		return;
	eglMakeCurrent(fDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
	if (fContext != NULL)
		eglDestroyContext(fDisplay, fContext);
	eglTerminate(fDisplay);
	fDisplay = NULL;
	fContext = NULL;
	fProgram = 0;
}


bool
KmsGles::_MakeCurrent(KmsGlTarget* target)
{
	if (!eglMakeCurrent(fDisplay, target->eglSurface, target->eglSurface,
			fContext))
		return false;
	if (fProgram != 0)
		return true;

	// the first time: the program and what the context can do
	const char* renderer = (const char*)glGetString(GL_RENDERER);
	strlcpy(fRenderer, renderer != NULL ? renderer : "?", sizeof(fRenderer));
	fSoftware = strstr(fRenderer, "llvmpipe") != NULL
		|| strstr(fRenderer, "softpipe") != NULL
		|| strstr(fRenderer, "Software") != NULL;
	const char* extensions = (const char*)glGetString(GL_EXTENSIONS);
	fUnpackSubimage = has_extension(extensions, "GL_EXT_unpack_subimage");
	GLint maxSize = 0;
	glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxSize);
	fMaxTextureSize = maxSize;

	GLuint vertex = compile_shader(GL_VERTEX_SHADER, kVertexShader);
	GLuint fragment = compile_shader(GL_FRAGMENT_SHADER, kFragmentShader);
	if (vertex == 0 || fragment == 0)
		return false;
	GLuint program = glCreateProgram();
	glAttachShader(program, vertex);
	glAttachShader(program, fragment);
	glBindAttribLocation(program, 0, "position");
	glLinkProgram(program);
	glDeleteShader(vertex);
	glDeleteShader(fragment);
	GLint linked = 0;
	glGetProgramiv(program, GL_LINK_STATUS, &linked);
	if (!linked) {
		glDeleteProgram(program);
		return false;
	}
	fProgram = program;
	fPositionLocation = 0;
	fTextureLocation = glGetUniformLocation(program, "source");
	TRACE("%s%s%s\n", fRenderer, fUnpackSubimage ? ", unpack subimage" : "",
		fBufferAge ? ", buffer age" : "");
	return true;
}


KmsGlTarget*
KmsGles::CreateTarget(uint32 width, uint32 height, uint32 regionWidth,
	uint32 regionHeight, const KmsRect& destination, status_t* _error)
{
	status_t error = B_NO_MEMORY;
	KmsGlTarget* target = NULL;
	if (fDisplay == NULL)
		error = B_NO_INIT;
	else if (fMaxTextureSize > 0 && ((int32)regionWidth > fMaxTextureSize
			|| (int32)regionHeight > fMaxTextureSize))
		error = B_BAD_VALUE;
	else
		target = new(std::nothrow) KmsGlTarget;
	if (target != NULL) {
		target->surface = NULL;
		target->eglSurface = EGL_NO_SURFACE;
		target->texture = 0;
		target->frames = 0;
		target->width = width;
		target->height = height;
		target->regionWidth = regionWidth;
		target->regionHeight = regionHeight;
		target->destination = destination;
		target->surface = gbm_surface_create(fDevice, width, height,
			GBM_FORMAT_XRGB8888, GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);
		if (target->surface != NULL) {
			target->eglSurface = eglCreateWindowSurface(fDisplay,
				(EGLConfig)fConfig, (EGLNativeWindowType)target->surface,
				NULL);
		}
		error = target->eglSurface != EGL_NO_SURFACE
			&& target->surface != NULL ? B_OK : B_NOT_SUPPORTED;
		if (error == B_OK && !_MakeCurrent(target))
			error = B_ERROR;
		if (error == B_OK && fMaxTextureSize > 0
			&& ((int32)regionWidth > fMaxTextureSize
				|| (int32)regionHeight > fMaxTextureSize))
			error = B_BAD_VALUE;
		if (error == B_OK) {
			glGenTextures(1, &target->texture);
			glBindTexture(GL_TEXTURE_2D, target->texture);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,
				GL_CLAMP_TO_EDGE);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,
				GL_CLAMP_TO_EDGE);
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, regionWidth, regionHeight,
				0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
			if (glGetError() != GL_NO_ERROR)
				error = B_NO_MEMORY;
		}
		if (error != B_OK) {
			DestroyTarget(target);
			target = NULL;
		}
	}
	if (_error != NULL)
		*_error = error;
	return target;
}


void
KmsGles::DestroyTarget(KmsGlTarget* target)
{
	if (target == NULL)
		return;
	if (target->texture != 0 && _MakeCurrent(target))
		glDeleteTextures(1, &target->texture);
	eglMakeCurrent(fDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
	if (target->eglSurface != EGL_NO_SURFACE && target->eglSurface != NULL)
		eglDestroySurface(fDisplay, target->eglSurface);
	if (target->surface != NULL)
		gbm_surface_destroy(target->surface);
	delete target;
}


void
KmsGles::_Upload(KmsGlTarget* target, const uint8* source,
	uint32 sourceBytesPerRow, const KmsRect& rect)
{
	const uint8* first = source + (size_t)rect.y * sourceBytesPerRow
		+ (size_t)rect.x * 4;
	if (fUnpackSubimage) {
		glPixelStorei(GL_UNPACK_ROW_LENGTH_EXT, sourceBytesPerRow / 4);
		glTexSubImage2D(GL_TEXTURE_2D, 0, rect.x, rect.y, rect.width,
			rect.height, GL_RGBA, GL_UNSIGNED_BYTE, first);
		glPixelStorei(GL_UNPACK_ROW_LENGTH_EXT, 0);
		return;
	}
	// rows of the rectangle together first
	size_t rowBytes = (size_t)rect.width * 4;
	fStaging.resize(rowBytes * rect.height);
	for (int32 y = 0; y < rect.height; y++) {
		memcpy(&fStaging[y * rowBytes], first + (size_t)y * sourceBytesPerRow,
			rowBytes);
	}
	glTexSubImage2D(GL_TEXTURE_2D, 0, rect.x, rect.y, rect.width, rect.height,
		GL_RGBA, GL_UNSIGNED_BYTE, &fStaging[0]);
}


gbm_bo*
KmsGles::Render(KmsGlTarget* target, const uint8* source,
	uint32 sourceBytesPerRow, const std::vector<KmsRect>& damage, bool full)
{
	if (!_MakeCurrent(target))
		return NULL;

	glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
	glBindTexture(GL_TEXTURE_2D, target->texture);
	KmsRect regionBounds(0, 0, target->regionWidth, target->regionHeight);
	KmsRect changed;
	if (full) {
		_Upload(target, source, sourceBytesPerRow, regionBounds);
		changed = KmsRect(0, 0, target->width, target->height);
	} else {
		for (size_t i = 0; i < damage.size(); i++) {
			KmsRect rect = damage[i].Intersect(regionBounds);
			if (rect.IsEmpty())
				continue;
			_Upload(target, source, sourceBytesPerRow, rect);
			changed = changed.Union(kms_map_damage(rect, regionBounds,
				target->destination));
		}
	}

	// what this buffer misses: the frames since it was last shown
	EGLint age = 0;
	if (fBufferAge)
		eglQuerySurface(fDisplay, target->eglSurface, EGL_BUFFER_AGE_EXT, &age);
	KmsRect repaint = changed;
	bool whole = full || age <= 0 || age > target->frames
		|| age > kDamageHistory + 1;
	if (whole)
		repaint = KmsRect(0, 0, target->width, target->height);
	else {
		for (int32 i = 0; i < age - 1; i++)
			repaint = repaint.Union(target->history[i]);
	}
	for (int32 i = kDamageHistory - 1; i > 0; i--)
		target->history[i] = target->history[i - 1];
	target->history[0] = changed;
	target->frames++;

	glViewport(0, 0, target->width, target->height);
	if (whole) {
		glDisable(GL_SCISSOR_TEST);
		glClearColor(0, 0, 0, 1);
		glClear(GL_COLOR_BUFFER_BIT);
	}
	if (!repaint.IsEmpty()) {
		glEnable(GL_SCISSOR_TEST);
		// GL's rows go upwards
		glScissor(repaint.x, target->height - repaint.Bottom(), repaint.width,
			repaint.height);
		const KmsRect& d = target->destination;
		glViewport(d.x, target->height - d.Bottom(), d.width, d.height);
		static const GLfloat kQuad[] = { 0, 0, 1, 0, 0, 1, 1, 1 };
		glUseProgram(fProgram);
		glUniform1i(fTextureLocation, 0);
		glActiveTexture(GL_TEXTURE0);
		glVertexAttribPointer(fPositionLocation, 2, GL_FLOAT, GL_FALSE, 0,
			kQuad);
		glEnableVertexAttribArray(fPositionLocation);
		glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
		glDisableVertexAttribArray(fPositionLocation);
		glDisable(GL_SCISSOR_TEST);
	}
	if (!eglSwapBuffers(fDisplay, target->eglSurface)) {
		TRACE("eglSwapBuffers: 0x%x\n", eglGetError());
		return NULL;
	}
	return gbm_surface_lock_front_buffer(target->surface);
}


void
KmsGles::ReleaseFrame(KmsGlTarget* target, gbm_bo* bo)
{
	if (target != NULL && bo != NULL)
		gbm_surface_release_buffer(target->surface, bo);
}
