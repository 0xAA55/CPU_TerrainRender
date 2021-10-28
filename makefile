CC = gcc
LD = gcc
CFLAGS = -openmp -O3 -flto -IC_dict -Irttimer -Imathutil -mavx
LDLIBS = -Lrttimer -lrttimer -Lmathutil -lmathutil -LC_dict -lcdict -lkernel32 -lgdi32 -luser32

OBJS = cpucan.o crc3264.o dictcfg.o game.o imgbuffer.o logprintf.o main.o raymap.o unibmp.o

all: CPU_RENDERER_V2.exe

CPU_RENDERER_V2.exe: $(OBJS) rttimer/librttimer.a mathutil/libmathutil.a C_dict/libcdict.a
	$(LD) $^ -o $@ $(LDLIBS)

rttimer/librttimer.a:
	$(MAKE) -C rttimer

mathutil/libmathutil.a:
	$(MAKE) -C mathutil

C_dict/libcdict.a:
	$(MAKE) -C C_dict

clean:
	$(MAKE) -C rttimer clean
	$(MAKE) -C mathutil clean
	$(MAKE) -C C_dict clean
	rm *.o CPU_RENDERER_V2.exe
