TARGET			=	libfreetype-gl.a
SOURCES			=	$(wildcard *.c)
OBJ_PATH		?=  obj
CC				?=  gcc
CFLAGS			?=	-O0
SYSTEM_LIBS    	+=  freetype2
INCLUDE_DIRS 	=   .
ARFLAGS 		=	rcs

LIBS_URLS		+=	https://github.com/AIG-Livny/cgeom.git

###

OBJECTS=$(SOURCES:%.c=$(OBJ_PATH)/%.o)
DEPS=$(OBJECTS:.o=.d)
DEPFLAGS=-MMD -MP
LIBS_PATHS = $(foreach url,$(LIBS_URLS),$(shell basename $(url) .git))

SPACE=$() $()
export PKG_CONFIG_PATH := $(subst $(SPACE),:,$(strip $(LIBS_PATHS)))

PC_PATTERN = $(addsuffix /*.pc, $(LIBS_PATHS))
CFLAGS += $(shell pkg-config --cflags $(PC_PATTERN) $(SYSTEM_LIBS))

$(foreach url,$(LIBS_URLS),$(eval URL_$(shell basename $(url) .git) := $(url)))

CFLAGS += $(addprefix -I, $(INCLUDE_DIRS))

.PHONY: all clean cleanall

all: $(LIBS_PATHS) $(TARGET)

clean:
	rm -rf $(OBJ_PATH) $(LIBS_PATHS)

cleanall: clean
	rm -rf $(TARGET)

$(LIBS_PATHS):
	$(if $(wildcard ../$@), ln -s ../$@ $@, git clone $(URL_$@) $@ 	)
	$(MAKE) -C $@
	$(MAKE)

$(TARGET): $(OBJECTS)
	$(AR) $(ARFLAGS) $@ $(OBJECTS)

$(OBJ_PATH)/%.o: %.c Makefile
	mkdir -p $(dir $@)
	$(CC) $(DEPFLAGS) $(CFLAGS) -c $< -o $@

-include $(DEPS)