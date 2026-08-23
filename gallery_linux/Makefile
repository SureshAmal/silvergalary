CXX ?= g++
CXXFLAGS ?= -std=c++17 -O3 -Wall -Wno-unused-function -Wno-unused-result
INCLUDES = -I../include -I../include/easyexif -I../include/GLFW -I../viewer_linux
LIBS = -L/lib64 -l:libglfw.so.3 -lGL -lsqlite3 -lm -lpthread -ldl

SRCS = main.cpp ../include/easyexif/exif.cpp
TARGET = ../bin/silver_gallery

all: $(TARGET)

$(TARGET): $(SRCS)
	@mkdir -p ../bin
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(SRCS) $(LIBS) -o $(TARGET)
	@ln -sf silver_gallery ../bin/silver_gallery_linux
	@echo "Gallery build successful: $(TARGET)"

clean:
	rm -f $(TARGET) ../bin/silver_gallery_linux

run: all
	$(TARGET)

.PHONY: all clean run
