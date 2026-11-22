# Copyright 2026, Koan contributors.
# Distributed under the terms of the MIT License.

PrintDriverAddOn(print_pcl6 "PCL6 Compatible"
	SOURCES
	DeltaRowCompression.cpp
	PCL6Entry.cpp
	PCL6.cpp
	PCL6Cap.cpp
	PCL6Rasterizer.cpp
	PCL6Writer.cpp
	Rasterizer.cpp

	RDEF PCL6.rdef
)
