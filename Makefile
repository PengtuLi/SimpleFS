# Configuration

CC		= gcc
LD		= gcc
# ar (archiver) 作为创建静态库 (.a 文件) 的工具
AR		= ar
# 位置无关代码
CFLAGS		= -g -std=gnu99 -Wall -Iinclude -fPIC
# lib file location
LDFLAGS		= -Llib
# default lib math
LIBS		= -lm
# replace/create if not exist/create index
ARFLAGS		= rcs

# print log
ifndef DEBUG_LOG
	CFLAGS += -DNDEBUG
endif

# Variables

SFS_LIB_HDRS	= $(wildcard include/sfs/*.h)
SFS_LIB_SRCS	= $(wildcard src/library/*.c)
SFS_LIB_OBJS	= $(SFS_LIB_SRCS:.c=.o)
SFS_LIBRARY	= lib/libsfs.a

SFS_SHL_SRCS	= $(wildcard src/shell/*.c)
SFS_SHL_OBJS	= $(SFS_SHL_SRCS:.c=.o)
SFS_SHELL	= bin/sfssh

SFS_TEST_SRCS   = $(wildcard src/tests/*.c)
SFS_TEST_OBJS   = $(SFS_TEST_SRCS:.c=.o)
# SFS_UNIT_TESTS	= $(patsubst src/tests/%,bin/%,$(patsubst %.c,%,$(wildcard src/tests/unit_*.c)))
SFS_UNIT_TESTS	= $(patsubst src/tests/%.c,bin/%,$(wildcard src/tests/unit_*.c))

# Rules

all:		$(SFS_LIBRARY) $(SFS_UNIT_TESTS) $(SFS_SHELL)

%.o:		%.c $(SFS_LIB_HDRS)
	@echo "Compiling $@"
	@$(CC) $(CFLAGS) -c -o $@ $<

$(SFS_LIBRARY):	$(SFS_LIB_OBJS)
	@echo "Linking   $@"
	@$(AR) $(ARFLAGS) $@ $^

$(SFS_SHELL):	$(SFS_SHL_OBJS) $(SFS_LIBRARY)
	@echo "Linking   $@"
	@$(LD) $(LDFLAGS) -o $@ $^ $(LIBS)

bin/unit_%:	src/tests/unit_%.o $(SFS_LIBRARY)
	@echo "Linking   $@"
	@$(LD) $(LDFLAGS) -o $@ $^

test-unit:	$(SFS_UNIT_TESTS)
	@EXIT=0; for test in bin/unit_*; do 		\
	    for i in $$(seq 0 $$($${test} 2>&1 | tail -n 1 | awk '{print $$1}')); do \
		echo "Running   $$(basename $$test) $$i";		\
		valgrind --leak-check=full $$test $$i > test.log 2>&1;	\
		EXIT=$$(($$EXIT + $$?));				\
		grep -q 'ERROR SUMMARY: 0' test.log || cat test.log;	\
		! grep -q 'Assertion' test.log || cat test.log; 	\
	    done				\
	done; exit $$EXIT

test-shell:	$(SFS_SHELL)
	@EXIT=0; for test in bin/test_*.sh; do	\
	    $${test};				\
	    EXIT=$$(($$EXIT + $$?));		\
	done; exit $$EXIT

test:	test-unit test-shell

clean:
	@echo "Removing  objects"
	@rm -f $(SFS_LIB_OBJS) $(SFS_SHL_OBJS) $(SFS_TEST_OBJS)

	@echo "Removing  libraries"
	@rm -f $(SFS_LIBRARY)

	@echo "Removing  programs"
	@rm -f $(SFS_SHELL)

	@echo "Removing  tests"
	@rm -f $(SFS_UNIT_TESTS) test.log

	@echo "recover data/diskfile."
	git restore --source=35d2ea2e02ee5f66e9469054721a848842c49c87 data/image*

.PRECIOUS: %.o
