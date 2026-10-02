// MallocBuffer.h

#include <OS.h>

#include "MallocBuffer.h"

// TODO: maybe this class could be more flexible by taking
// a color_space argument in the constructor
// the hardcoded width * 4 (because that's how it's used now anyways)
// could be avoided, but I'm in a hurry... :-)

// constructor
//
// The pixels are an area of their own rather than a heap block: a direct
// window that draws in frame buffer pixels on a scaled desktop is given this
// buffer to keep up to date as well (DirectWindowInfo), and handing out the
// area around a heap block would hand out everything else in it too.
MallocBuffer::MallocBuffer(uint32 width,
						   uint32 height)
	: fBuffer(NULL),
	  fArea(-1),
	  fWidth(width),
	  fHeight(height)
{
	if (fWidth > 0 && fHeight > 0) {
		size_t size = ((size_t)fWidth * 4 * fHeight + B_PAGE_SIZE - 1)
			& ~(size_t)(B_PAGE_SIZE - 1);
		fArea = create_area("back buffer", &fBuffer, B_ANY_ADDRESS, size,
			B_NO_LOCK, B_READ_AREA | B_WRITE_AREA);
		if (fArea < 0)
			fBuffer = NULL;
	}
}

// destructor
MallocBuffer::~MallocBuffer()
{
	if (fArea >= 0)
		delete_area(fArea);
}

// InitCheck
status_t
MallocBuffer::InitCheck() const
{
	return fBuffer ? B_OK : B_NO_MEMORY;
}

// ColorSpace
color_space
MallocBuffer::ColorSpace() const
{
	return B_RGBA32;
}

// Bits
void*
MallocBuffer::Bits() const
{
	if (InitCheck() >= B_OK)
		return fBuffer;
	return NULL;
}

// BytesPerRow
uint32
MallocBuffer::BytesPerRow() const
{
	if (InitCheck() >= B_OK)
		return fWidth * 4;
	return 0;
}

// Width
uint32
MallocBuffer::Width() const
{
	if (InitCheck() >= B_OK)
		return fWidth;
	return 0;
}

// Height
uint32
MallocBuffer::Height() const
{
	if (InitCheck() >= B_OK)
		return fHeight;
	return 0;
}

