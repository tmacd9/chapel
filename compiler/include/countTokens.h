/*
 * Copyright 2004-2015 Cray Inc.
 * Other additional copyright holders may be indicated within.
 * 
 * The entirety of this work is licensed under the Apache License,
 * Version 2.0 (the "License"); you may not use this file except
 * in compliance with the License.
 * 
 * You may obtain a copy of the License at
 * 
 *     http://www.apache.org/licenses/LICENSE-2.0
 * 
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef _COUNTTOKENS_H_
#define _COUNTTOKENS_H_

/* BLC: This file contains routines that help keep track of statistics
   on numbers of tokens and to print out the code in a tokenized
   form */

extern bool countTokens;
extern bool printTokens;

void startCountingFileTokens(const char* filename);
void stopCountingFileTokens(void);
void finishCountingTokens(void);

void countToken(const char* tokentext);
void countNewline(void);
void countCommentLine(void);
void countSingleLineComment(const char* comment);
void countMultiLineComment(char* comment);

#endif
