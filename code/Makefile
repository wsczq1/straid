###############################################################################
#
# A smart Makefile template for GNU/LINUX programming
#
# Author: PRC (ijkxyz AT msn DOT com)
# Date:   2011/06/17
#
# Usage:
#   $ make           Compile and link (or archive)
#   $ make clean     Clean the objectives and target.
###############################################################################

CROSS_COMPILE =
OPTIMIZE := -O3 
WARNINGS := -Wall -Wno-unused-variable -Wno-unused-but-set-variable -Wno-sign-compare -Wno-comment
EXTRA_CFLAGS := -g -std=c++2a
LIBS := -lpthread -lisal -luring -ltbb -lcds

INC_DIR   = ./include ./include/concurrentqueue ./include/Bitmap
SRC_DIR   =	./src ./include
OBJ_DIR   = ./obj
BIN_DIR   = ./bin
EXTRA_SRC = 
EXCLUDE_FILES = 
EXCLUDE_OBJS = obj/main.o obj/bench.o obj/trace_md.o obj/trace_st.o
SUFFIX       = c cc

TARGET := main bench trace_md trace_st
TARGET_TYPE  := app


#####################################################################################
#  Do not change any part of them unless you have understood this script very well  #
#  This is a kind remind.                                                           #
#####################################################################################

#FUNC#  Add a new line to the input stream.
define add_newline
$1

endef

#FUNC# set the variable `src-x' according to the input $1
define set_src_x
src-$1 = $(filter-out $4,$(foreach d,$2,$(wildcard $d/*.$1)) $(filter %.$1,$3))

endef

#FUNC# set the variable `obj-x' according to the input $1
define set_obj_x
obj-$1 = $(patsubst %.$1,$3%.o,$(notdir $2))

endef

#VAR# Get the uniform representation of the object directory path name
ifneq ($(OBJ_DIR),)
prefix_objdir  = $(shell echo $(OBJ_DIR)|sed 's:\(\./*\)*::')
prefix_objdir := $(filter-out /,$(prefix_objdir)/)
endif

GCC      := $(CROSS_COMPILE)gcc
G++      := $(CROSS_COMPILE)g++
SRC_DIR := $(sort . $(SRC_DIR))
inc_dir = $(foreach d,$(sort $(INC_DIR) $(SRC_DIR)),-I$d)

#--# Do smart deduction automatically
$(eval $(foreach i,$(SUFFIX),$(call set_src_x,$i,$(SRC_DIR),$(EXTRA_SRC),$(EXCLUDE_FILES))))
$(eval $(foreach i,$(SUFFIX),$(call set_obj_x,$i,$(src-$i),$(prefix_objdir))))
$(eval $(foreach f,$(EXTRA_SRC),$(call add_newline,vpath $(notdir $f) $(dir $f))))
$(eval $(foreach d,$(SRC_DIR),$(foreach i,$(SUFFIX),$(call add_newline,vpath %.$i $d))))

all_objs = $(foreach i,$(SUFFIX),$(obj-$i))
all_srcs = $(foreach i,$(SUFFIX),$(src-$i))
share_objs = $(filter-out $(EXCLUDE_OBJS), $(all_objs))

CFLAGS       = $(EXTRA_CFLAGS) $(WARNINGS) $(OPTIMIZE)
GCCFLAGS	 = $(OPTIMIZE)
TARGET_TYPE := $(strip $(TARGET_TYPE))

ifeq ($(filter $(TARGET_TYPE),so ar app),)
$(error Unexpected TARGET_TYPE `$(TARGET_TYPE)')
endif

PHONY = all .mkdir clean

all: .mkdir $(TARGET)

define cmd_o
$$(obj-$1): $2%.o: %.$1  $(MAKEFILE_LIST)
ifeq ($1,c)
	$(GCC) $(inc_dir) -Wp,-MT,$$@ -Wp,-MMD,$$@.d $(GCCFLAGS) -c -o $$@ $$< -Wno-format 
else
	$(G++) $(inc_dir) -Wp,-MT,$$@ -Wp,-MMD,$$@.d $(CFLAGS) -c -o $$@ $$<
endif

endef
$(eval $(foreach i,$(SUFFIX),$(call cmd_o,$i,$(prefix_objdir))))


define cmd_t
$1: $(share_objs) $(OBJ_DIR)/$1.o
	$(G++) $(LDFLAGS) $(share_objs) $(OBJ_DIR)/$1.o -o $(BIN_DIR)/$$@ $(LIBS)

endef
$(eval $(foreach i,$(TARGET),$(call cmd_t,$i)))

.mkdir:
	@if [ ! -d $(OBJ_DIR) ]; then mkdir -p $(OBJ_DIR); fi
	@if [ ! -d $(BIN_DIR) ]; then mkdir -p $(BIN_DIR); fi

clean:
	rm -f $(prefix_objdir)*.o $(prefix_objdir)*.d $(patsubst %,$(BIN_DIR)/%,$(TARGET))

-include $(patsubst %.o,%.o.d,$(all_objs))

.PHONY: $(PHONY)
