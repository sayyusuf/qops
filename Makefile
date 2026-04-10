MODULE_NAME =  qops
NAME = qops
_SRC = qops.c
_INC = qops.h


EXLIBS += -lpthread -latomic

EXTRAFLAGS += -O3

ifdef _ADDRESS 
DEBUGFLAGS += -g  -fsanitize=address
else
DEBUGFLAGS += -g  -fsanitize=thread
endif

#DEP2 = 
#DEP2LINK = 


DEPS= \
#	DEP1	\
#	DEP2	\
#	DEP3

include modMakefile
