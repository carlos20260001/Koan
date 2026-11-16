/*
 * Copyright 2026, air/OS contributors.
 * Distributed under the terms of the MIT License.
 */


/*!	A speech model's directory: its description (speech.conf, lines of a
	key and values), the vocabulary and the mel filter bank. Everything
	read here is checked: the files come from wherever models come from,
	and the numbers in them end up as indices and sizes.
*/


#include <SpeechCore.h>

#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


namespace BPrivate {


namespace {


// more than any model has by far; what keeps a damaged file from asking
// for the memory of the machine
static const int32 kMaxVocabulary = 1 << 20;
static const size_t kMaxVocabularyFile = 64 << 20;
static const int32 kMaxLanguages = 1024;
static const int32 kMaxLineLength = 1 << 20;


struct Key {
	const char*	name;
	size_t		offset;
	//! 0: a number, otherwise the size of the string
	size_t		size;
	//! numbers: what they may be
	int32		min;
	int32		max;
};

#define NUMBER(field, min, max) \
	{ #field, offsetof(SpeechModelInfo, field), 0, min, max }
#define STRING(field) \
	{ #field, offsetof(SpeechModelInfo, field), \
		sizeof(((SpeechModelInfo*)0)->field), 0, 0 }

static const Key kKeys[] = {
	STRING(family),
	STRING(name),
	STRING(encoder),
	STRING(decoder),
	STRING(vocabulary),
	STRING(mel_filters),
	NUMBER(sample_rate, 8000, 48000),
	NUMBER(fft_size, 16, 4096),
	NUMBER(hop, 1, 4096),
	NUMBER(mel_bins, 1, 512),
	NUMBER(window_frames, 1, 30000),
	NUMBER(text_context, 2, 65536),
	NUMBER(max_tokens, 1, 65536),
	NUMBER(timestamp_ms, 1, 1000),
	NUMBER(vocabulary_size, 2, kMaxVocabulary),
	NUMBER(token_eot, 0, kMaxVocabulary),
	NUMBER(token_sot, 0, kMaxVocabulary),
	NUMBER(token_translate, 0, kMaxVocabulary),
	NUMBER(token_transcribe, 0, kMaxVocabulary),
	NUMBER(token_no_speech, 0, kMaxVocabulary),
	NUMBER(token_no_timestamps, 0, kMaxVocabulary),
	NUMBER(token_timestamp_begin, 0, kMaxVocabulary),
	NUMBER(token_blank, 0, kMaxVocabulary)
};

#undef NUMBER
#undef STRING


/*!	The next word of a line (which is cut up in the process), NULL at
	its end or where a comment begins. */
static char*
next_word(char** _line)
{
	char* line = *_line;
	while (*line != '\0' && isspace((unsigned char)*line))
		line++;
	if (*line == '\0' || *line == '#')
		return NULL;

	char* word = line;
	while (*line != '\0' && !isspace((unsigned char)*line))
		line++;
	if (*line != '\0')
		*line++ = '\0';
	*_line = line;
	return word;
}


static bool
parse_number(const char* word, int32 min, int32 max, int32* _value)
{
	if (word == NULL)
		return false;
	char* end;
	errno = 0;
	long value = strtol(word, &end, 10);
	if (errno != 0 || end == word || *end != '\0' || value < min
		|| value > max) {
		return false;
	}
	*_value = (int32)value;
	return true;
}


//! A file name of the model's own directory, and nothing else.
static bool
is_plain_name(const char* name)
{
	return name[0] != '\0' && name[0] != '.' && strchr(name, '/') == NULL;
}


}	// unnamed namespace


SpeechModel::SpeechModel()
	:
	fFilters(NULL),
	fSuppress(NULL),
	fSuppressCount(0),
	fLanguages(NULL),
	fLanguageCount(0),
	fTokenBytes(NULL),
	fTokenOffsets(NULL),
	fTokenCount(0)
{
	memset(&fInfo, 0, sizeof(fInfo));
	fDirectory[0] = '\0';
	fError[0] = '\0';
}


SpeechModel::~SpeechModel()
{
	Unset();
}


void
SpeechModel::Unset()
{
	free(fFilters);
	free(fSuppress);
	free(fLanguages);
	free(fTokenBytes);
	free(fTokenOffsets);
	fFilters = NULL;
	fSuppress = NULL;
	fSuppressCount = 0;
	fLanguages = NULL;
	fLanguageCount = 0;
	fTokenBytes = NULL;
	fTokenOffsets = NULL;
	fTokenCount = 0;
	memset(&fInfo, 0, sizeof(fInfo));
	fDirectory[0] = '\0';
}


void
SpeechModel::_SetError(const char* format, ...)
{
	va_list args;
	va_start(args, format);
	vsnprintf(fError, sizeof(fError), format, args);
	va_end(args);
}


bool
SpeechModel::GetPath(const char* file, char* path, size_t size) const
{
	return (size_t)snprintf(path, size, "%s/%s", fDirectory, file) < size;
}


status_t
SpeechModel::Load(const char* directory)
{
	Unset();
	fError[0] = '\0';
	if (directory == NULL || strlen(directory) >= sizeof(fDirectory))
		return B_BAD_VALUE;
	strcpy(fDirectory, directory);

	char path[1280];
	GetPath("speech.conf", path, sizeof(path));
	status_t status = _LoadDescription(path);
	if (status == B_OK)
		status = _Check();
	if (status == B_OK) {
		GetPath(fInfo.vocabulary, path, sizeof(path));
		status = _LoadVocabulary(path);
	}
	if (status == B_OK) {
		GetPath(fInfo.mel_filters, path, sizeof(path));
		status = _LoadFilters(path);
	}
	if (status != B_OK) {
		char error[sizeof(fError)];
		strcpy(error, fError);
		Unset();
		strcpy(fError, error);
	}
	return status;
}


status_t
SpeechModel::_LoadDescription(const char* path)
{
	FILE* file = fopen(path, "r");
	if (file == NULL) {
		_SetError("no speech.conf in the model directory");
		return B_NAME_NOT_FOUND;
	}

	// a mark for every key that must be there
	const size_t kKeyCount = sizeof(kKeys) / sizeof(kKeys[0]);
	bool seen[sizeof(kKeys) / sizeof(kKeys[0])] = {};

	status_t status = B_OK;
	char* line = NULL;
	size_t capacity = 0;
	ssize_t length;
	int lineNumber = 0;
	while (status == B_OK && (length = getline(&line, &capacity, file)) >= 0) {
		lineNumber++;
		if (length > kMaxLineLength) {
			status = B_BAD_DATA;
			break;
		}

		char* rest = line;
		const char* key = next_word(&rest);
		if (key == NULL)
			continue;

		if (strcmp(key, "suppress") == 0) {
			// any number of tokens, on any number of lines
			const char* word;
			while ((word = next_word(&rest)) != NULL) {
				int32 token;
				if (!parse_number(word, 0, kMaxVocabulary, &token)
					|| fSuppressCount >= kMaxVocabulary) {
					status = B_BAD_DATA;
					break;
				}
				int32* list = (int32*)realloc(fSuppress,
					(fSuppressCount + 1) * sizeof(int32));
				if (list == NULL) {
					status = B_NO_MEMORY;
					break;
				}
				fSuppress = list;
				fSuppress[fSuppressCount++] = token;
			}
		} else if (strcmp(key, "language") == 0) {
			const char* code = next_word(&rest);
			int32 token;
			if (code == NULL || strlen(code) >= sizeof(fLanguages->code)
				|| !parse_number(next_word(&rest), 0, kMaxVocabulary, &token)
				|| fLanguageCount >= kMaxLanguages) {
				status = B_BAD_DATA;
				break;
			}
			SpeechLanguage* list = (SpeechLanguage*)realloc(fLanguages,
				(fLanguageCount + 1) * sizeof(SpeechLanguage));
			if (list == NULL) {
				status = B_NO_MEMORY;
				break;
			}
			fLanguages = list;
			strcpy(fLanguages[fLanguageCount].code, code);
			fLanguages[fLanguageCount++].token = token;
		} else if (strcmp(key, "multilingual") == 0) {
			int32 value = 0;
			if (!parse_number(next_word(&rest), 0, 1, &value))
				status = B_BAD_DATA;
			fInfo.multilingual = value != 0;
		} else {
			size_t i = 0;
			for (; i < kKeyCount; i++) {
				if (strcmp(kKeys[i].name, key) == 0)
					break;
			}
			// keys of later versions are passed over
			if (i == kKeyCount)
				continue;

			const char* value = next_word(&rest);
			char* field = (char*)&fInfo + kKeys[i].offset;
			if (value == NULL) {
				status = B_BAD_DATA;
			} else if (kKeys[i].size > 0) {
				if (strlen(value) >= kKeys[i].size)
					status = B_BAD_DATA;
				else
					strcpy(field, value);
			} else if (!parse_number(value, kKeys[i].min, kKeys[i].max,
					(int32*)field)) {
				status = B_BAD_DATA;
			}
			seen[i] = true;
		}

		if (status == B_BAD_DATA)
			_SetError("speech.conf, line %d: bad value of %s", lineNumber, key);
	}
	free(line);
	fclose(file);

	for (size_t i = 0; status == B_OK && i < kKeyCount; i++) {
		if (!seen[i]) {
			_SetError("speech.conf does not say %s", kKeys[i].name);
			status = B_BAD_DATA;
		}
	}
	return status;
}


//! Whether the numbers of the description go together.
status_t
SpeechModel::_Check()
{
	const SpeechModelInfo& info = fInfo;

	if (!is_plain_name(info.encoder) || !is_plain_name(info.decoder)
		|| !is_plain_name(info.vocabulary)
		|| !is_plain_name(info.mel_filters)) {
		_SetError("speech.conf names files outside of the model directory");
		return B_BAD_DATA;
	}

	// The text tokens come first, then the end of text and the other
	// special tokens, the time stamps last.
	int32 size = info.vocabulary_size;
	if (info.hop > info.fft_size || info.fft_size % 2 != 0
		|| info.token_eot >= info.token_timestamp_begin
		|| info.token_timestamp_begin >= size
		|| info.token_sot <= info.token_eot || info.token_sot >= size
		|| info.token_translate >= size || info.token_transcribe >= size
		|| info.token_no_speech >= size || info.token_no_timestamps >= size
		|| info.token_blank >= info.token_eot
		|| info.max_tokens + 4 > info.text_context) {
		_SetError("the numbers of speech.conf do not go together");
		return B_BAD_DATA;
	}

	for (int32 i = 0; i < fSuppressCount; i++) {
		if (fSuppress[i] >= size) {
			_SetError("speech.conf suppresses a token that is not there");
			return B_BAD_DATA;
		}
	}
	for (int32 i = 0; i < fLanguageCount; i++) {
		if (fLanguages[i].token >= size) {
			_SetError("speech.conf has a language without a token");
			return B_BAD_DATA;
		}
	}
	return B_OK;
}


/*!	The vocabulary: for each text token a byte of length and the bytes. */
status_t
SpeechModel::_LoadVocabulary(const char* path)
{
	FILE* file = fopen(path, "rb");
	if (file == NULL) {
		_SetError("no vocabulary file %s", fInfo.vocabulary);
		return B_NAME_NOT_FOUND;
	}

	status_t status = B_OK;
	long size = -1;
	if (fseek(file, 0, SEEK_END) == 0)
		size = ftell(file);
	if (size <= 0 || (size_t)size > kMaxVocabularyFile
		|| fseek(file, 0, SEEK_SET) != 0) {
		status = B_BAD_DATA;
	}

	int32 count = fInfo.token_eot;
	uint8* data = NULL;
	if (status == B_OK) {
		data = (uint8*)malloc(size);
		fTokenOffsets = (uint32*)malloc(((size_t)count + 1) * sizeof(uint32));
		if (data == NULL || fTokenOffsets == NULL)
			status = B_NO_MEMORY;
		else if (fread(data, 1, size, file) != (size_t)size)
			status = B_IO_ERROR;
	}
	fclose(file);

	// The bytes stay where they are; the lengths between them are what
	// the offsets step over.
	size_t at = 0;
	for (int32 i = 0; status == B_OK && i < count; i++) {
		if (at >= (size_t)size || at + 1 + data[at] > (size_t)size) {
			status = B_BAD_DATA;
			break;
		}
		fTokenOffsets[i] = at + 1;
		at += 1 + data[at];
	}
	if (status == B_OK && at != (size_t)size)
		status = B_BAD_DATA;

	if (status != B_OK) {
		if (status == B_BAD_DATA) {
			_SetError("%s does not hold the %d text tokens of the model",
				fInfo.vocabulary, (int)count);
		}
		free(data);
		return status;
	}

	fTokenOffsets[count] = at + 1;
	fTokenBytes = data;
	fTokenCount = count;
	return B_OK;
}


status_t
SpeechModel::_LoadFilters(const char* path)
{
	FILE* file = fopen(path, "rb");
	if (file == NULL) {
		_SetError("no filter bank file %s", fInfo.mel_filters);
		return B_NAME_NOT_FOUND;
	}

	size_t count = (size_t)fInfo.mel_bins * (fInfo.fft_size / 2 + 1);
	fFilters = (float*)malloc(count * sizeof(float));
	status_t status = B_OK;
	if (fFilters == NULL)
		status = B_NO_MEMORY;
	else if (fread(fFilters, sizeof(float), count, file) != count
		|| fgetc(file) != EOF) {
		status = B_BAD_DATA;
	}
	fclose(file);

	// weights of a filter are shares of the power: not negative, finite
	for (size_t i = 0; status == B_OK && i < count; i++) {
		if (!(fFilters[i] >= 0) || !isfinite(fFilters[i]))
			status = B_BAD_DATA;
	}
	if (status == B_BAD_DATA) {
		_SetError("%s is not a filter bank of %d by %d", fInfo.mel_filters,
			(int)fInfo.mel_bins, (int)(fInfo.fft_size / 2 + 1));
	}
	return status;
}


int32
SpeechModel::LanguageToken(const char* code) const
{
	if (code == NULL)
		return -1;
	for (int32 i = 0; i < fLanguageCount; i++) {
		if (strcmp(fLanguages[i].code, code) == 0)
			return fLanguages[i].token;
	}
	return -1;
}


status_t
SpeechModel::DecodeTokens(const int32* tokens, int32 count,
	char** _text) const
{
	size_t length = 0;
	for (int32 i = 0; i < count; i++) {
		if (tokens[i] >= 0 && tokens[i] < fTokenCount) {
			length += fTokenOffsets[tokens[i] + 1] - fTokenOffsets[tokens[i]]
				- 1;
		}
	}

	uint8* bytes = (uint8*)malloc(length + 1);
	char* text = (char*)malloc(3 * length + 1);
	if (bytes == NULL || text == NULL) {
		free(bytes);
		free(text);
		return B_NO_MEMORY;
	}

	size_t at = 0;
	for (int32 i = 0; i < count; i++) {
		if (tokens[i] >= 0 && tokens[i] < fTokenCount) {
			size_t size = fTokenOffsets[tokens[i] + 1]
				- fTokenOffsets[tokens[i]] - 1;
			memcpy(bytes + at, fTokenBytes + fTokenOffsets[tokens[i]], size);
			at += size;
		}
	}

	text[speech_sanitize_utf8(bytes, length, text)] = '\0';
	free(bytes);
	*_text = text;
	return B_OK;
}


size_t
speech_sanitize_utf8(const uint8* bytes, size_t length, char* output)
{
	size_t written = 0;
	size_t i = 0;
	while (i < length) {
		uint8 lead = bytes[i];
		if (lead < 0x80) {
			// (a zero byte would end the string early)
			output[written++] = lead != 0 ? (char)lead : ' ';
			i++;
			continue;
		}

		// how many bytes follow the first, and what the second may be
		// (which is where the encodings that must not be are caught:
		// longer than needed, the surrogates, beyond U+10FFFF)
		int following = 0;
		uint8 low = 0x80;
		uint8 high = 0xbf;
		if (lead >= 0xc2 && lead <= 0xdf) {
			following = 1;
		} else if (lead >= 0xe0 && lead <= 0xef) {
			following = 2;
			if (lead == 0xe0)
				low = 0xa0;
			else if (lead == 0xed)
				high = 0x9f;
		} else if (lead >= 0xf0 && lead <= 0xf4) {
			following = 3;
			if (lead == 0xf0)
				low = 0x90;
			else if (lead == 0xf4)
				high = 0x8f;
		}

		// as far as the bytes could still become a character
		size_t valid = 1;
		bool complete = following > 0;
		for (int j = 0; j < following; j++) {
			if (i + valid >= length) {
				complete = false;
				break;
			}
			uint8 byte = bytes[i + valid];
			if (byte < (j == 0 ? low : 0x80) || byte > (j == 0 ? high : 0xbf)) {
				complete = false;
				break;
			}
			valid++;
		}

		if (complete) {
			memcpy(output + written, bytes + i, valid);
			written += valid;
		} else {
			// U+FFFD, the replacement character
			output[written++] = (char)0xef;
			output[written++] = (char)0xbf;
			output[written++] = (char)0xbd;
		}
		i += valid;
	}
	return written;
}


}	// namespace BPrivate
