# Copyright 2026, Koan contributors. All rights reserved.
# Distributed under the terms of the MIT License.

# The target neural is the kit (libneural.so).
Application(neural_cli SOURCES neural.cpp LIBS neural media translation)
set_target_properties(neural_cli PROPERTIES OUTPUT_NAME neural)
UseMediaKit(neural_cli)
target_include_directories(neural_cli PRIVATE ${PROJECT_SOURCE_DIR}/headers/private/neural)
