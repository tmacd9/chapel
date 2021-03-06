# Copyright 2004-2015 Cray Inc.
# Other additional copyright holders may be indicated within.
# 
# The entirety of this work is licensed under the Apache License,
# Version 2.0 (the "License"); you may not use this file except
# in compliance with the License.
# 
# You may obtain a copy of the License at
# 
#     http://www.apache.org/licenses/LICENSE-2.0
# 
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

ifndef CHPL_MAKE_HOME
export CHPL_MAKE_HOME=$(shell pwd)/../..
endif

COMPILER_ROOT = ..
COMPILER_SUBDIR = parser

#
# standard header
#
include $(COMPILER_ROOT)/make/Makefile.compiler.head

PARSER_OBJDIR = $(OBJ_SUBDIR)
include Makefile.share

TARGETS = $(PARSER_OBJS)

include $(COMPILER_ROOT)/make/Makefile.compiler.subdirrules

ifdef CHPL_DEVELOPER
CLEAN_TARGS += \
	*.tab.cpp \
	*.yy.cpp \
	*.tab.h \
	*.output \

endif

lex.yy.cpp: chapel.lex chapel.tab.h
	flex -o$@ chapel.lex

chapel.tab.h chapel.tab.cpp: chapel.ypp
	bison --defines=chapel.tab.h -v -t $<
	@if [ `grep "conflicts:" chapel.output | wc -l` -ne 0 ]; then echo "PROBLEM: chapel.y contains conflicts"; exit 1; fi;


FORCE:

#
# standard footer
#
include $(COMPILER_ROOT)/make/Makefile.compiler.foot


# Use chapel.tab.h as prerequisite, to ensure bison only invoked once, even
# with parallel execution.
#
# no-error required here for bison 1.875 (not 1.875d)
$(OBJ_SUBDIR)/chapel.tab.o: chapel.tab.h $(OBJ_SUBDIR_MADE) $(LEXYACC_INCLUDES)
	$(CXX) -c $(COMP_CFLAGS) $(COMP_CFLAGS_NONCHPL) -o $@ chapel.tab.cpp $(OBJ_SUBDIR_MADE) $(LEXYACC_INCLUDES)


# no-error required here for flex 2.5.33
$(OBJ_SUBDIR)/lex.yy.o: lex.yy.cpp $(OBJ_SUBDIR_MADE) chapel.tab.h $(LEXYACC_INCLUDES)
	$(CXX) -c $(COMP_CFLAGS) $(COMP_CFLAGS_NONCHPL) -o $@ $<

#
# need to handle these ones specially as well on intel because it
# #includes chapel.tab.h
#
$(OBJ_SUBDIR)/parser.o: parser.cpp $(OBJ_SUBDIR_MADE) chapel.tab.h
	$(CXX) -c $(COMP_CFLAGS) $(COMP_CFLAGS_NONCHPL) -o $@ $<

$(OBJ_SUBDIR)/processTokens.o: processTokens.cpp $(OBJ_SUBDIR_MADE) chapel.tab.h
	$(CXX) -c $(COMP_CFLAGS) $(COMP_CFLAGS_NONCHPL) -o $@ $<
