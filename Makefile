MODULE_NAME =  qops
NAME = qops
_SRC = qops.c
_INC = qops.h

EXLIBS += -lpthread

ifdef _ADDRESS 
EXTRAFLAGS +=  -O3
DEBUGFLAGS += -g  -fsanitize=address
else
EXTRAFLAGS +=  -O3
DEBUGFLAGS += -g  -fsanitize=thread
endif

#DEP2 = 
#DEP2LINK = 


DEPS= \
#	DEP1	\
#	DEP2	\
#	DEP3

include modMakefile
