PREFIX?=	/usr/local
INSTALL?=	/usr/bin/install

PROG = agurim
CFLAGS = -O3 -Wall -DNDEBUG
CC=gcc

UTIL_DIR=util

AGURIM_OBJS = agurim.o
AGURIM_OBJS += agurim_param.o 

AGURIM_OBJS += agurim_odflow.o
AGURIM_OBJS += $(UTIL_DIR)/odflow_list.o $(UTIL_DIR)/odflow_hash.o 

AGURIM_OBJS += agurim_hhh.o 
AGURIM_OBJS += $(UTIL_DIR)/hhh_task.o $(UTIL_DIR)/hhh_util.o

AGURIM_OBJS += agurim_plot.o 
AGURIM_OBJS += $(UTIL_DIR)/plot_aguri.o $(UTIL_DIR)/plot_json.o $(UTIL_DIR)/plot_csv.o

AGURIM_OBJS += agurim_file.o
AGURIM_OBJS += $(UTIL_DIR)/file_string.o

#CFLAGS = -g -Wall 

all: $(PROG)

#agurim: -I./util/
agurim: $(AGURIM_OBJS) 
	$(CC) $(CFLAGS) -o $@ $(AGURIM_OBJS) -lm

install: $(PROG)
	$(INSTALL) -m 0755 $(PROG) $(PREFIX)/bin

clean:;	-rm -f $(PROG) *.o $(UTIL_DIR)/*.o core *.core *~
