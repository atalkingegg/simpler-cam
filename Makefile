.phony := all clean

PROGS := simpler-cam

CXXFLAGS := -Wall -std=c++17 -I/usr/local/include/libcamera
CXXLDFLAGS := -L/usr/local/lib -lcamera-base -lcamera -levent -levent_pthreads -lpng -lpthread

all : ${PROGS}

simpler-cam: simpler-cam.cpp
	g++ ${CXXFLAGS} -o $@ $< ${CXXLDFLAGS}

clean:
	rm -f ${PROGS}
