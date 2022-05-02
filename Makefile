.SUFFIXES : .c .o

BASE = $(shell pwd)

Q_		  ?= 

#CC        := arm-linux-gnueabihf-gcc
CC        := gcc
MKDIR     := mkdir -p
CP 		  := cp

CFLAGS    := -O2 -Wall -fPIC
LDFLAGS   := -lpthread -ldl

OUT_DIR   := obj
TARGET    := memory_hook.so

DEFINES   := -D_GNU_SOURCE
INCLUDES  := $(BASE)
SRC_DIRS  := $(BASE)
SRCS      := memory_hook.c
SRCS      += shared_library.c
SRCS      += sc_cli.c
SRCS      += sc_readline.c


#### DO NOT MODIFY ###########
APP       := $(OUT_DIR)/$(TARGET)
APP_OBJS  := $(SRCS:%.c=$(OUT_DIR)/%.o) 
APP_FLAGS := $(DEFINES) $(addprefix -I, $(INCLUDES))


vpath %.c   $(SRC_DIRS)

.PHONY: all clean

all: $(OUT_DIR) $(APP)

$(APP): $(APP_OBJS)
	@echo "[Linking... $(notdir $(APP))]"
	$(Q_)$(CC) -shared -o $(APP) $(APP_OBJS) $(LDFLAGS) 
	
$(OUT_DIR):
	$(Q_)$(MKDIR) $(OUT_DIR)
	
$(OUT_DIR)/%.o : %.c 
	@echo "[Compile... $(notdir $<)]" 
	$(Q_)$(CC) $(CFLAGS) $(APP_FLAGS) -c $< -o $@ 

clean:
	@echo "[Clean... all objs]"
	$(Q_)-$(RM) -rf $(OUT_DIR)

