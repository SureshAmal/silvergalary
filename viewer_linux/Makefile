CXX ?= g++
CXXFLAGS ?= -std=c++17 -O3 -Wall -Wno-unused-function -Wno-unused-result
INCLUDES = -I../include -I../include/easyexif -I../include/GLFW
LIBS = -L/lib64 -l:libglfw.so.3 -lGL -lm -lpthread -ldl

SRCS = main.cpp ../include/easyexif/exif.cpp
TARGET = ../bin/silver_viewer

all: $(TARGET)

$(TARGET): $(SRCS)
	@mkdir -p ../bin
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(SRCS) $(LIBS) -o $(TARGET)
	@ln -sf silver_viewer ../bin/silver_viewer_linux
	@echo "Build successful: $(TARGET)"

clean:
	rm -f $(TARGET) ../bin/silver_viewer_linux

run: all
	$(TARGET)

.PHONY: all clean run
