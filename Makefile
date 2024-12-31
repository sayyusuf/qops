MODULE_NAME =  qops
NAME = qops
_SRC = qops.c
_INC = qops.h

EXLIBS += -lpthread

EXTRAFLAGS +=  -O3
DEBUGFLAGS += -g  -fsanitize=thread



#DEP2 = 
#DEP2LINK = 


DEPS= \
#	DEP1	\
#	DEP2	\
#	DEP3

include modMakefile
