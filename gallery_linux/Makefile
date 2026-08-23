CC ?= gcc
CXX ?= g++
CFLAGS ?= -O3 -Wall -Wno-unused-function -Wno-unused-result
CXXFLAGS ?= -std=c++17 -O3 -Wall -Wno-unused-function -Wno-unused-result
INCLUDES = -I../include -I../include/easyexif -I../include/GLFW -I../viewer_linux
LIBS = ../lib/sqlite3.o ../lib/libglfw3.a -lGL -lm -lpthread -ldl

SRCS = main.cpp ../include/easyexif/exif.cpp
TARGET = ../bin/silver_gallery

all: $(TARGET)

../lib/sqlite3.o: ../include/sqlite3.c
	@mkdir -p ../lib
	$(CC) $(CFLAGS) -I../include -c $< -o $@

$(TARGET): $(SRCS) ../lib/sqlite3.o ../lib/libglfw3.a
	@mkdir -p ../bin
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(SRCS) $(LIBS) -o $(TARGET)
	@ln -sf silver_gallery ../bin/silver_gallery_linux
	@echo "Gallery build successful: $(TARGET)"

clean:
	rm -f $(TARGET) ../bin/silver_gallery_linux

run: all
	$(TARGET)

.PHONY: all clean run
