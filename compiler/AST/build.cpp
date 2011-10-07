#include "build.h"
#include "astutil.h"
#include "baseAST.h"
#include "expr.h"
#include "parser.h"
#include "stmt.h"
#include "stringutil.h"
#include "symbol.h"
#include "type.h"
#include "config.h"

static void
checkControlFlow(Expr* expr, const char* context) {
  Vec<const char*> labelSet; // all labels in expr argument
  Vec<BaseAST*> loopSet;     // all asts in a loop in expr argument
  Vec<BaseAST*> innerFnSet;  // all asts in a function in expr argument
  Vec<BaseAST*> asts;
  collect_asts(expr, asts);

  //
  // compute labelSet and loopSet
  //
  forv_Vec(BaseAST, ast, asts) {
    if (DefExpr* def = toDefExpr(ast)) {
      if (LabelSymbol* ls = toLabelSymbol(def->sym))
        labelSet.set_add(ls->name);
      else if (FnSymbol* fn = toFnSymbol(def->sym)) {
        if (!innerFnSet.set_in(fn)) {
          Vec<BaseAST*> innerAsts;
          collect_asts(fn, innerAsts);
          forv_Vec(BaseAST, ast, innerAsts) {
            innerFnSet.set_add(ast);
          }
        }
      }
    } else if (BlockStmt* block = toBlockStmt(ast)) {
      if (block->isLoop() && !loopSet.set_in(block)) {
        if (block->userLabel != NULL) {
          labelSet.set_add(block->userLabel);
        }
        Vec<BaseAST*> loopAsts;
        collect_asts(block, loopAsts);
        forv_Vec(BaseAST, ast, loopAsts) {
          loopSet.set_add(ast);
        }
      }
    }
  }

  //
  // check for illegal control flow
  //
  forv_Vec(BaseAST, ast, asts) {
    if (CallExpr* call = toCallExpr(ast)) {
      if (innerFnSet.set_in(call))
        continue; // yield or return is in nested function/iterator
      if (call->isPrimitive(PRIM_RETURN)) {
        USR_FATAL_CONT(call, "return is not allowed in %s", context);
      } else if (call->isPrimitive(PRIM_YIELD)) {
        if (!strcmp(context, "begin statement") ||
            !strcmp(context, "yield statement"))
          USR_FATAL_CONT(call, "yield is not allowed in %s", context);
      }
    } else if (GotoStmt* gs = toGotoStmt(ast)) {
      if (labelSet.set_in(gs->getName()))
        continue; // break or continue target is in scope
      if (toSymExpr(gs->label) && toSymExpr(gs->label)->var == gNil && loopSet.set_in(gs))
        continue; // break or continue loop is in scope
      if (!strcmp(context, "on statement")) {
        USR_PRINT(gs, "the following error is a current limitation");
      }
      if (gs->gotoTag == GOTO_BREAK) {
        USR_FATAL_CONT(gs, "break is not allowed in %s", context);
      } else if (gs->gotoTag == GOTO_CONTINUE) {
        USR_FATAL_CONT(gs, "continue is not allowed in %s", context);
      } else {
        USR_FATAL_CONT(gs, "illegal 'goto' usage; goto is deprecated anyway");
      }
    }
  }
}


static void addPragmaFlags(Symbol* sym, Vec<const char*>* pragmas) {
  forv_Vec(const char, str, *pragmas) {
    Flag flag = pragma2flag(str);
    if (flag == FLAG_UNKNOWN)
      USR_FATAL_CONT(sym, "unknown pragma: \"%s\"", str);
    else
      sym->addFlag(flag);
  }
}

BlockStmt* buildPragmaStmt(BlockStmt* block,
                           Vec<const char*>* pragmas,
                           BlockStmt* stmt) {
  if (DefExpr* def = toDefExpr(stmt->body.first()))
    addPragmaFlags(def->sym, pragmas);
  else if (pragmas->n > 0) {
    USR_FATAL_CONT(stmt, "cannot attach pragmas to this statement");
    USR_PRINT(stmt, "   %s \"%s\"",
              pragmas->n == 1 ? "pragma" : "starting with pragma",
              pragmas->v[0]);
  }
  delete pragmas;
  block->insertAtTail(stmt);
  return block;
}


Expr* buildParenExpr(CallExpr* call) {
  if (call->numActuals() == 1)
    return call->get(1)->remove();
  else
    return new CallExpr("_build_tuple", call);
}


Expr* buildSquareCallExpr(Expr* base, CallExpr* args) {
  CallExpr* call = new CallExpr(base, args);
  call->square = true;
  return call;
}


Expr* buildNamedActual(const char* name, Expr* expr) {
  return new NamedExpr(name, expr);
}


Expr* buildNamedAliasActual(const char* name, Expr* expr) {
  return new CallExpr(PRIM_ACTUALS_LIST,
           new NamedExpr(name, expr),
           new NamedExpr(astr("chpl__aliasField_", name), new SymExpr(gTrue)));
}


Expr* buildFormalArrayType(Expr* iterator, Expr* eltType, Expr* index) {
  if (index) {
    CallExpr* indexCall = toCallExpr(index);
    INT_ASSERT(indexCall);
    if (indexCall->numActuals() != 1)
      USR_FATAL(iterator, "invalid index expression");
    return new CallExpr("chpl__buildArrayRuntimeType",
             new CallExpr("chpl__buildDomainExpr", iterator),
             eltType, indexCall->get(1)->remove(),
             new CallExpr("chpl__buildDomainExpr", iterator->copy()));
  } else {
    CallExpr* call = toCallExpr(iterator);
    if (call->numActuals() == 1 && isDefExpr(call->get(1))) {
      return new CallExpr("chpl__buildArrayRuntimeType", call->get(1)->remove(), eltType);
    } else
      return new CallExpr("chpl__buildArrayRuntimeType",
               new CallExpr("chpl__buildDomainExpr", iterator), eltType);
  }
}

Expr* buildIntLiteral(const char* pch) {
  uint64_t ull;
  if (!strncmp("0b", pch, 2))
    ull = binStr2uint64(pch);
  else if (!strncmp("0x", pch, 2))
    ull = hexStr2uint64(pch);
  else
    ull = str2uint64(pch);
  if (ull <= 2147483647ull)
    return new SymExpr(new_IntSymbol(ull, INT_SIZE_32));
  else if (ull <= 9223372036854775807ull)
    return new SymExpr(new_IntSymbol(ull, INT_SIZE_64));
  else
    return new SymExpr(new_UIntSymbol(ull, INT_SIZE_64));
}


Expr* buildRealLiteral(const char* pch) {
  return new SymExpr(new_RealSymbol(pch, strtod(pch, NULL)));
}


Expr* buildImagLiteral(const char* pch) {
  char* str = strdup(pch);
  str[strlen(pch)-1] = '\0';
  SymExpr* se = new SymExpr(new_ImagSymbol(str, strtod(str, NULL)));
  free(str);
  return se;
}


Expr* buildStringLiteral(const char* pch) {
  return new SymExpr(new_StringSymbol(pch));
}


Expr* buildDotExpr(BaseAST* base, const char* member) {
  // The following optimization was added to avoid calling chpl_int_to_locale
  // when all we end up doing is extracting the locale id, thus:
  // chpl_int_to_locale(_get_locale(x)).id ==> _get_locale(x)

  // This broke when realms were removed and uid was renamed as id.
  // It might be better coding practice to label very special module code
  // (i.e. types, fields, values known to the compiler) using pragmas. <hilde>
  if (!strcmp("id", member))
    if (CallExpr* intToLocale = toCallExpr(base))
      if (intToLocale->isNamed("chpl_int_to_locale"))
        if (CallExpr* getLocale = toCallExpr(intToLocale->get(1)))
          if (getLocale->isPrimitive(PRIM_GET_LOCALEID))
            return getLocale->remove();

  // "x.locale" member access expressions are rendered as chpl_int_to_locale(_get_locale(x)).
  if (!strcmp("locale", member))
    return new CallExpr("chpl_int_to_locale", 
                        new CallExpr(PRIM_GET_LOCALEID, base));
  else
    return new CallExpr(".", base, new_StringSymbol(member));
}


Expr* buildDotExpr(const char* base, const char* member) {
  return buildDotExpr(new UnresolvedSymExpr(base), member);
}


Expr* buildLogicalAndExpr(BaseAST* left, BaseAST* right) {
  VarSymbol* lvar = newTemp();
  lvar->addFlag(FLAG_MAYBE_PARAM);
  FnSymbol* ifFn = buildIfExpr(new CallExpr("isTrue", lvar),
                                 new CallExpr("isTrue", right),
                                 new SymExpr(gFalse));
  ifFn->insertAtHead(new CondStmt(new CallExpr("_cond_invalid", lvar), new CallExpr("compilerError", new_StringSymbol("cannot promote short-circuiting && operator"))));
  ifFn->insertAtHead(new CallExpr(PRIM_MOVE, lvar, left));
  ifFn->insertAtHead(new DefExpr(lvar));
  return new CallExpr(new DefExpr(ifFn));
}


Expr* buildLogicalOrExpr(BaseAST* left, BaseAST* right) {
  VarSymbol* lvar = newTemp();
  lvar->addFlag(FLAG_MAYBE_PARAM);
  FnSymbol* ifFn = buildIfExpr(new CallExpr("isTrue", lvar),
                                 new SymExpr(gTrue),
                                 new CallExpr("isTrue", right));
  ifFn->insertAtHead(new CondStmt(new CallExpr("_cond_invalid", lvar), new CallExpr("compilerError", new_StringSymbol("cannot promote short-circuiting || operator"))));
  ifFn->insertAtHead(new CallExpr(PRIM_MOVE, lvar, left));
  ifFn->insertAtHead(new DefExpr(lvar));
  return new CallExpr(new DefExpr(ifFn));
}


BlockStmt* buildChapelStmt(BaseAST* ast) {
  BlockStmt* block = NULL;
  if (!ast)
    block = new BlockStmt();
  else if (Expr* a = toExpr(ast))
    block = new BlockStmt(a);
  else
    INT_FATAL(ast, "Illegal argument to buildChapelStmt");
  block->blockTag = BLOCK_SCOPELESS;
  return block;
}


static void addModuleToSearchList(CallExpr* newUse, BaseAST* module) {
  UnresolvedSymExpr* modNameExpr = toUnresolvedSymExpr(module);
  if (modNameExpr) {
    addModuleToParseList(modNameExpr->unresolved, newUse);
  } else if (CallExpr* callExpr = toCallExpr(module)) {
    addModuleToSearchList(newUse, callExpr->argList.first());
  }
}


static BlockStmt* buildUseList(BaseAST* module, BlockStmt* list) {
  CallExpr* newUse = new CallExpr(PRIM_USE, module);
  addModuleToSearchList(newUse, module);
  if (list == NULL) {
    return buildChapelStmt(newUse);
  } else {
    list->insertAtTail(newUse);
    return list;
  }
}


BlockStmt* buildUseStmt(CallExpr* modules) {
  BlockStmt* list = NULL;
  for_actuals(expr, modules)
    list = buildUseList(expr->remove(), list);
  return list;
}


static void
buildTupleVarDeclHelp(Expr* base, BlockStmt* decls, Expr* insertPoint) {
  int count = 1;
  for_alist(expr, decls->body) {
    if (DefExpr* def = toDefExpr(expr)) {
      if (strcmp(def->sym->name, "chpl__tuple_blank")) {
        def->init = new CallExpr(base->copy(), new_IntSymbol(count));
        insertPoint->insertBefore(def->remove());
      } else {
        def->remove();
      }
    } else if (BlockStmt* blk = toBlockStmt(expr)) {
      buildTupleVarDeclHelp(new CallExpr(base, new_IntSymbol(count)),
                            blk, insertPoint);
    } else {
      INT_FATAL(expr, "unexpected expression in buildTupleVarDeclHelp");
    }
    count++;
  }
  decls->remove();
}


BlockStmt*
buildTupleVarDeclStmt(BlockStmt* tupleBlock, Expr* type, Expr* init) {
  VarSymbol* tmp = newTemp();
  int count = 1;
  for_alist(expr, tupleBlock->body) {
    if (DefExpr* def = toDefExpr(expr)) {
      if (strcmp(def->sym->name, "chpl__tuple_blank")) {
        def->init = new CallExpr(tmp, new_IntSymbol(count));
      } else {
        def->remove();
      }
    } else if (BlockStmt* blk = toBlockStmt(expr)) {
      buildTupleVarDeclHelp(new CallExpr(tmp, new_IntSymbol(count)), blk, expr);
    }
    count++;
  }
  //
  // Add compiler errors if tmp is not a tuple or if tmp.size is not the
  // same as the number of variables.  These checks will get inserted in
  // buildVarDecls after it asserts that only DefExprs are in this block.
  //
  tupleBlock->blockInfo = new CallExpr("_check_tuple_var_decl", tmp, new_IntSymbol(count-1));
  tupleBlock->insertAtHead(new DefExpr(tmp, init, type));
  return tupleBlock;
}


BlockStmt*
buildLabelStmt(const char* name, Expr* stmt) {
  BlockStmt* block = toBlockStmt(stmt);
  if (block) {
    Expr* breakLabelStmt = block->body.tail;
    if (!isDefExpr(breakLabelStmt) && isDefExpr(breakLabelStmt->prev)) {
      // the last statement in the block could be a call to _freeIterator()
      breakLabelStmt = breakLabelStmt->prev;
    }
    BlockStmt* loop = toBlockStmt(breakLabelStmt->prev);
    if (loop && loop->isLoop() &&
         (loop->blockInfo->isPrimitive(PRIM_BLOCK_FOR_LOOP)     ||
          loop->blockInfo->isPrimitive(PRIM_BLOCK_WHILEDO_LOOP) ||
          loop->blockInfo->isPrimitive(PRIM_BLOCK_DOWHILE_LOOP))) {
      if (!loop->breakLabel || !loop->continueLabel) {
        USR_FATAL(stmt, "cannot label parallel loop");
      } else {
        loop->userLabel = astr(name);
      }
    } else {
      USR_FATAL(stmt, "cannot label non-loop statement");
    }
  } else {
    USR_FATAL(stmt, "cannot label non-loop statement");
  }
  return block;
}


BlockStmt*
buildIfStmt(Expr* condExpr, Expr* thenExpr, Expr* elseExpr) {
  if (UnresolvedSymExpr* use = toUnresolvedSymExpr(condExpr))
    if (!strcmp(use->unresolved, gTryToken->name))
      return buildChapelStmt(new CondStmt(condExpr, thenExpr, elseExpr));
  return buildChapelStmt(new CondStmt(new CallExpr("_cond_test", condExpr), thenExpr, elseExpr));
}


void createInitFn(ModuleSymbol* mod) {
  SET_LINENO(mod);

  mod->initFn = new FnSymbol(astr("chpl__init_", mod->name));
  mod->initFn->retType = dtVoid;
  mod->initFn->addFlag(FLAG_MODULE_INIT);
  mod->initFn->addFlag(FLAG_INSERT_LINE_FILE_INFO);
  mod->initFn->addFlag(FLAG_PROC_ITER_KW_USED); // ProcIter: remove

  //
  // move module-level statements into module's init function
  //
  if (mod != theProgram) {
    for_alist(stmt, mod->block->body) {

      //
      // except for module definitions
      //
      if (BlockStmt* block = toBlockStmt(stmt))
        if (block->length() == 1)
          if (DefExpr* def = toDefExpr(block->body.only()))
            if (isModuleSymbol(def->sym))
              continue;

      mod->initFn->insertAtTail(stmt->remove());
    }
  }
  mod->block->insertAtHead(new DefExpr(mod->initFn));
}


ModuleSymbol* buildModule(const char* name, BlockStmt* block, const char* filename) {
  ModuleSymbol* mod = new ModuleSymbol(name, currentModuleType, block);
  mod->filename = astr(filename);
  createInitFn(mod);
  return mod;
}


CallExpr* buildPrimitiveExpr(CallExpr* exprs) {
  INT_ASSERT(exprs->isPrimitive(PRIM_ACTUALS_LIST));
  if (exprs->argList.length == 0)
    INT_FATAL("primitive has no name");
  Expr* expr = exprs->get(1);
  expr->remove();
  SymExpr* symExpr = toSymExpr(expr);
  if (!symExpr)
    INT_FATAL(expr, "primitive has no name");
  VarSymbol* var = toVarSymbol(symExpr->var);
  if (!var || !var->immediate || var->immediate->const_kind != CONST_KIND_STRING)
    INT_FATAL(expr, "primitive with non-literal string name");
  PrimitiveOp* prim = primitives_map.get(var->immediate->v_string);
  if (!prim)
    INT_FATAL(expr, "primitive not found '%s'", var->immediate->v_string);
  return new CallExpr(prim, exprs);
}


BlockStmt* buildPrimitiveLoopStmt(CallExpr* exprs, BlockStmt* body) {
  checkControlFlow(body, "xmt pragma forall i in n");

  INT_ASSERT(exprs->isPrimitive(PRIM_ACTUALS_LIST));
  if (exprs->argList.length == 0)
    INT_FATAL("primitive has no name");

  SymExpr* primname = toSymExpr(exprs->get(1));
  primname->remove();
  if (!primname) INT_FATAL(primname, "primitive has no name");
  VarSymbol* var = toVarSymbol(primname->var);
  if (!var ||
      !var->immediate || 
      var->immediate->const_kind != CONST_KIND_STRING)
    INT_FATAL(primname, "primitive with non-literal string name");
  PrimitiveOp* prim = primitives_map.get(var->immediate->v_string);
  if (!prim) INT_FATAL("primitive not found '%s'", var->immediate->v_string);

  BlockStmt* beginBlk = new BlockStmt();
  if (!(strcmp(var->immediate->v_string, "xmt pragma forall i in n"))) {
    // XMT pragma formed via #pragma mta for all streams i of n
    // 1st argument is the unique ID per_streams_i (from 0 to n-1)
    Expr* indices = toExpr(exprs->get(1));
    indices->remove();
    // 2nd argument is total_streams_n (akin to numChunks)
    Expr* iterator = toExpr(exprs->get(1));
    iterator->remove();

    beginBlk->blockInfo = new CallExpr(prim->tag, indices, iterator);
    beginBlk->insertAtHead(body);
  } else {
    INT_FATAL("primitive not yet implemented '%s'", var->immediate->v_string);
  }

  return beginBlk;
}


FnSymbol* buildIfExpr(Expr* e, Expr* e1, Expr* e2) {
  static int uid = 1;

  if (!e2)
    USR_FATAL("if-then expressions currently require an else-clause");

  FnSymbol* ifFn = new FnSymbol(astr("_if_fn", istr(uid++)));
  ifFn->addFlag(FLAG_COMPILER_NESTED_FUNCTION);
  ifFn->addFlag(FLAG_INLINE);
  VarSymbol* tmp1 = newTemp();
  VarSymbol* tmp2 = newTemp();
  tmp1->addFlag(FLAG_MAYBE_PARAM);
  tmp2->addFlag(FLAG_MAYBE_TYPE);

  ifFn->addFlag(FLAG_MAYBE_PARAM);
  ifFn->addFlag(FLAG_MAYBE_TYPE);
  ifFn->insertAtHead(new DefExpr(tmp1));
  ifFn->insertAtHead(new DefExpr(tmp2));
  ifFn->insertAtTail(new CallExpr(PRIM_MOVE, new SymExpr(tmp1), new CallExpr("_cond_test", e)));
  ifFn->insertAtTail(new CondStmt(
    new SymExpr(tmp1),
    new CallExpr(PRIM_MOVE,
                 new SymExpr(tmp2),
                 new CallExpr(PRIM_LOGICAL_FOLDER,
                              new SymExpr(tmp1),
                              new CallExpr(PRIM_GET_REF, e1))),
    new CallExpr(PRIM_MOVE,
                 new SymExpr(tmp2),
                 new CallExpr(PRIM_LOGICAL_FOLDER,
                              new SymExpr(tmp1),
                              new CallExpr(PRIM_GET_REF, e2)))));
  ifFn->insertAtTail(new CallExpr(PRIM_RETURN, tmp2));
  return ifFn;
}


CallExpr* buildLetExpr(BlockStmt* decls, Expr* expr) {
  static int uid = 1;
  FnSymbol* fn = new FnSymbol(astr("_let_fn", istr(uid++)));
  fn->addFlag(FLAG_COMPILER_NESTED_FUNCTION);
  fn->addFlag(FLAG_INLINE);
  fn->insertAtTail(decls);
  fn->insertAtTail(new CallExpr(PRIM_RETURN, expr));
  return new CallExpr(new DefExpr(fn));
}


BlockStmt* buildWhileDoLoopStmt(Expr* cond, BlockStmt* body) {
  cond = new CallExpr("_cond_test", cond);
  VarSymbol* condVar = newTemp();
  body = new BlockStmt(body);
  body->blockInfo = new CallExpr(PRIM_BLOCK_WHILEDO_LOOP, condVar);
  LabelSymbol* continueLabel = new LabelSymbol("_continueLabel");
  continueLabel->addFlag(FLAG_TEMP);
  continueLabel->addFlag(FLAG_LABEL_CONTINUE);
  body->continueLabel = continueLabel;
  LabelSymbol* breakLabel = new LabelSymbol("_breakLabel");
  breakLabel->addFlag(FLAG_TEMP);
  breakLabel->addFlag(FLAG_LABEL_BREAK);
  body->breakLabel = breakLabel;
  body->insertAtTail(new DefExpr(continueLabel));
  body->insertAtTail(new CallExpr(PRIM_MOVE, condVar, cond->copy()));
  BlockStmt* stmts = buildChapelStmt();
  stmts->insertAtTail(new DefExpr(condVar));
  stmts->insertAtTail(new CallExpr(PRIM_MOVE, condVar, cond->copy()));
  stmts->insertAtTail(body);
  stmts->insertAtTail(new DefExpr(breakLabel));
  return stmts;
}


BlockStmt* buildDoWhileLoopStmt(Expr* cond, BlockStmt* body) {
  cond = new CallExpr("_cond_test", cond);
  VarSymbol* condVar = newTemp();

  // make variables declared in the scope of the body visible to
  // expressions in the condition of a do..while block
  if (body->length() == 1 && toBlockStmt(body->body.only())) {
    body = toBlockStmt(body->body.only());
    body->remove();
  }

  LabelSymbol* continueLabel = new LabelSymbol("_continueLabel");
  continueLabel->addFlag(FLAG_TEMP);
  continueLabel->addFlag(FLAG_LABEL_CONTINUE);
  LabelSymbol* breakLabel = new LabelSymbol("_breakLabel");
  breakLabel->addFlag(FLAG_TEMP);
  breakLabel->addFlag(FLAG_LABEL_BREAK);
  BlockStmt* block = new BlockStmt(body);
  block->continueLabel = continueLabel;
  block->breakLabel = breakLabel;
  block->blockInfo = new CallExpr(PRIM_BLOCK_DOWHILE_LOOP, condVar);
  BlockStmt* stmts = buildChapelStmt();
  stmts->insertAtTail(new DefExpr(condVar));
  stmts->insertAtTail(block);
  body->insertAtTail(new DefExpr(continueLabel));
  body->insertAtTail(new CallExpr(PRIM_MOVE, condVar, cond->copy()));
  stmts->insertAtTail(new DefExpr(breakLabel));
  return stmts;
}


BlockStmt* buildSerialStmt(Expr* cond, BlockStmt* body) {
  cond = new CallExpr("_cond_test", cond);
  if (fSerial) {
    body->insertAtHead(cond);
    return body;
  } else {
    BlockStmt *sbody = new BlockStmt();
    VarSymbol *serial_state = newTemp();
    sbody->insertAtTail(new DefExpr(serial_state, new CallExpr(PRIM_GET_SERIAL)));
    sbody->insertAtTail(new CondStmt(cond, new CallExpr(PRIM_SET_SERIAL, gTrue)));
    sbody->insertAtTail(body);
    sbody->insertAtTail(new CallExpr(PRIM_SET_SERIAL, serial_state));
    return sbody;
  }
}


//
// check validity of indices in loops and expressions
//
static void
checkIndices(BaseAST* indices) {
  if (CallExpr* call = toCallExpr(indices)) {
    if (!call->isNamed("_build_tuple"))
      USR_FATAL(indices, "invalid index expression");
    for_actuals(actual, call)
      checkIndices(actual);
  } else if (!isSymExpr(indices) && !isUnresolvedSymExpr(indices))
    USR_FATAL(indices, "invalid index expression");
}


static void
destructureIndices(BlockStmt* block,
                   BaseAST* indices,
                   Expr* init,
                   bool coforall) {
  if (CallExpr* call = toCallExpr(indices)) {
    if (call->isNamed("_build_tuple")) {
      int i = 1;
      for_actuals(actual, call) {
        if (UnresolvedSymExpr* use = toUnresolvedSymExpr(actual)) {
          if (!strcmp(use->unresolved, "chpl__tuple_blank")) {
            i++;
            continue;
          }
        }
        destructureIndices(block, actual,
                           new CallExpr(init->copy(), new_IntSymbol(i)),
                           coforall);
        i++;
      }
    }
  } else if (UnresolvedSymExpr* sym = toUnresolvedSymExpr(indices)) {
    VarSymbol* var = new VarSymbol(sym->unresolved);
    block->insertAtHead(new CallExpr(PRIM_MOVE, var, init));
    block->insertAtHead(new DefExpr(var));
    var->addFlag(FLAG_INDEX_VAR);
    if (coforall)
      var->addFlag(FLAG_HEAP_ALLOCATE);
    var->addFlag(FLAG_INSERT_AUTO_DESTROY);
  } else if (SymExpr* sym = toSymExpr(indices)) {
    block->insertAtHead(new CallExpr(PRIM_MOVE, sym->var, init));
    sym->var->addFlag(FLAG_INDEX_VAR);
    if (coforall)
      sym->var->addFlag(FLAG_HEAP_ALLOCATE);
    sym->var->addFlag(FLAG_INSERT_AUTO_DESTROY);
  }
}


static BlockStmt*
handleArrayTypeCase(FnSymbol* fn, Expr* indices, Expr* iteratorExpr, Expr* expr) {
  BlockStmt* block = new BlockStmt();
  fn->addFlag(FLAG_MAYBE_TYPE);
  bool hasSpecifiedIndices = !!indices;
  if (!hasSpecifiedIndices)
    indices = new UnresolvedSymExpr("chpl__elidedIdx");
  checkIndices(indices);

  //
  // nested function to compute isArrayType which is set to true if
  // the inner expression is a type and false otherwise
  //
  // this nested function is called in a type block so that it is
  // never executed; placing all this code in a separate function
  // inside the type block is essential for two reasons:
  //
  // first, so that the iterators in any nested parallel loop
  // expressions are not pulled all the way out during cleanup
  //
  // second, so that types and functions declared in this nested
  // function do not get removed from the IR when the type lbock gets
  // removed
  //
  FnSymbol* isArrayTypeFn = new FnSymbol("_isArrayTypeFn");
  isArrayTypeFn->addFlag(FLAG_INLINE);
  isArrayTypeFn->addFlag(FLAG_PROC_ITER_KW_USED); // ProcIter: remove

  Symbol* isArrayType = newTemp("_isArrayType");
  isArrayType->addFlag(FLAG_MAYBE_PARAM);
  fn->insertAtTail(new DefExpr(isArrayType));

  VarSymbol* iteratorSym = newTemp("_iterator");
  isArrayTypeFn->insertAtTail(new DefExpr(iteratorSym));
  isArrayTypeFn->insertAtTail(new CallExpr(PRIM_MOVE, iteratorSym,
                                new CallExpr("_getIterator", iteratorExpr->copy())));
  VarSymbol* index = newTemp("_indexOfInterest");
  isArrayTypeFn->insertAtTail(new DefExpr(index));
  isArrayTypeFn->insertAtTail(new CallExpr(PRIM_MOVE, index,
                                new CallExpr("iteratorIndex", iteratorSym)));
  BlockStmt* indicesBlock = new BlockStmt();
  destructureIndices(indicesBlock, indices->copy(), new SymExpr(index), false);
  indicesBlock->blockTag = BLOCK_SCOPELESS;
  isArrayTypeFn->insertAtTail(indicesBlock);
  isArrayTypeFn->insertAtTail(new CondStmt(
                                new CallExpr("chpl__isType", expr->copy()),
                                new CallExpr(PRIM_MOVE, isArrayType, gTrue),
                                new CallExpr(PRIM_MOVE, isArrayType, gFalse)));
  fn->insertAtTail(new DefExpr(isArrayTypeFn));
  BlockStmt* typeBlock = new BlockStmt();
  typeBlock->blockTag = BLOCK_TYPE;
  typeBlock->insertAtTail(new CallExpr(isArrayTypeFn));
  fn->insertAtTail(typeBlock);

  Symbol* arrayType = newTemp("_arrayType");
  arrayType->addFlag(FLAG_EXPR_TEMP);
  arrayType->addFlag(FLAG_MAYBE_TYPE);
  BlockStmt* thenStmt = new BlockStmt();
  thenStmt->insertAtTail(new DefExpr(arrayType));
  Symbol* domain = newTemp("_domain");
  domain->addFlag(FLAG_EXPR_TEMP);
  thenStmt->insertAtTail(new DefExpr(domain));
  // note that we need the below autoCopy until we start reference
  // counting domains within runtime array types
  thenStmt->insertAtTail(new CallExpr(PRIM_MOVE, domain,
                           new CallExpr("chpl__autoCopy",
                             new CallExpr("chpl__buildDomainExpr",
                                          iteratorExpr->copy()))));
  if (hasSpecifiedIndices) {
    // we want to swap something like the below commented-out
    // statement with the compiler error statement but skyline
    // arrays are not yet supported...
    thenStmt->insertAtTail(new CallExpr(PRIM_MOVE, arrayType, new CallExpr("compilerError", new_StringSymbol("unimplemented feature: if you are attempting to use skyline arrays, they are not yet supported; if not, remove the index expression from this array type specification"))));
    //      thenStmt->insertAtTail(new CallExpr(PRIM_MOVE, arrayType,
    //                                          new CallExpr("chpl__buildArrayRuntimeType",
    //                                                       domain, expr->copy(),
    //                                                       indices->copy(), domain)));
  } else {
    thenStmt->insertAtTail(new CallExpr(PRIM_MOVE, arrayType,
                             new CallExpr("chpl__buildArrayRuntimeType",
                                          domain, expr->copy())));
  }
  thenStmt->insertAtTail(new CallExpr(PRIM_RETURN, arrayType));
  fn->insertAtTail(new CondStmt(new SymExpr(isArrayType), thenStmt, block));
  return block;
}


static int loopexpr_uid = 1;

// builds body of for expression iterator
CallExpr*
buildForLoopExpr(Expr* indices, Expr* iteratorExpr, Expr* expr, Expr* cond, bool maybeArrayType) {
  FnSymbol* fn = new FnSymbol(astr("_seqloopexpr", istr(loopexpr_uid++)));
  fn->addFlag(FLAG_PROC_ITER_KW_USED); // ProcIter: remove
  BlockStmt* block = fn->body;

  if (maybeArrayType) {
    INT_ASSERT(!cond);
    block = handleArrayTypeCase(fn, indices, iteratorExpr, expr);
  }

  VarSymbol* iterator = newTemp("_iterator");
  iterator->addFlag(FLAG_EXPR_TEMP);
  block->insertAtTail(new DefExpr(iterator));
  block->insertAtTail(new CallExpr(PRIM_MOVE, iterator, new CallExpr("_checkIterator", iteratorExpr)));
  const char* iteratorName = astr("_iterator_for_loopexpr", istr(loopexpr_uid-1));
  block->insertAtTail(new CallExpr(PRIM_RETURN, new CallExpr(iteratorName, iterator)));

  //
  // build serial iterator function
  //
  FnSymbol* sifn = new FnSymbol(iteratorName);
  sifn->addFlag(FLAG_PROC_ITER_KW_USED); // ProcIter: remove
  sifn->addFlag(FLAG_ITERATOR_FN); // ProcIter: I think we should keep this one
  ArgSymbol* sifnIterator = new ArgSymbol(INTENT_BLANK, "iterator", dtAny);
  sifn->insertFormalAtTail(sifnIterator);
  fn->insertAtHead(new DefExpr(sifn));
  Expr* stmt = new CallExpr(PRIM_YIELD, expr);
  if (cond)
    stmt = new CondStmt(new CallExpr("_cond_test", cond), stmt);
  sifn->insertAtTail(buildForLoopStmt(indices, new SymExpr(sifnIterator), new BlockStmt(stmt)));
  return new CallExpr(new DefExpr(fn));
}


CallExpr*
buildForallLoopExpr(Expr* indices, Expr* iteratorExpr, Expr* expr, Expr* cond, bool maybeArrayType) {
  if (fSerial || fSerialForall)
    return buildForLoopExpr(indices, iteratorExpr, expr, cond, maybeArrayType);

  FnSymbol* fn = new FnSymbol(astr("_parloopexpr", istr(loopexpr_uid++)));
  fn->addFlag(FLAG_PROC_ITER_KW_USED); // ProcIter: remove
  BlockStmt* block = fn->body;

  if (maybeArrayType) {
    INT_ASSERT(!cond);
    block = handleArrayTypeCase(fn, indices, iteratorExpr, expr);
  }

  VarSymbol* iterator = newTemp("_iterator");
  iterator->addFlag(FLAG_EXPR_TEMP);
  block->insertAtTail(new DefExpr(iterator));
  block->insertAtTail(new CallExpr(PRIM_MOVE, iterator, new CallExpr("_checkIterator", iteratorExpr)));
  const char* iteratorName = astr("_iterator_for_loopexpr", istr(loopexpr_uid-1));
  block->insertAtTail(new CallExpr(PRIM_RETURN, new CallExpr(iteratorName, iterator)));

  //
  // build serial iterator function
  //
  FnSymbol* sifn = new FnSymbol(iteratorName);
  sifn->addFlag(FLAG_PROC_ITER_KW_USED); // ProcIter: remove
  sifn->addFlag(FLAG_ITERATOR_FN); // ProcIter: I think we should keep this one
  ArgSymbol* sifnIterator = new ArgSymbol(INTENT_BLANK, "iterator", dtAny);
  sifn->insertFormalAtTail(sifnIterator);
  fn->insertAtHead(new DefExpr(sifn));
  Expr* stmt = new CallExpr(PRIM_YIELD, expr);
  if (cond)
    stmt = new CondStmt(new CallExpr("_cond_test", cond), stmt);
  sifn->insertAtTail(buildForLoopStmt(indices, new SymExpr(sifnIterator), new BlockStmt(stmt)));

  //
  // build leader iterator function
  //
  FnSymbol* lifn = new FnSymbol(iteratorName);
  lifn->addFlag(FLAG_PROC_ITER_KW_USED); // ProcIter: remove
  ArgSymbol* lifnIterator = new ArgSymbol(INTENT_BLANK, "iterator", dtAny);
  lifn->insertFormalAtTail(lifnIterator);
  Expr* tag = buildDotExpr(buildDotExpr(new UnresolvedSymExpr("ChapelBase"),
                                        iterKindTypename),
                           iterKindLeaderTagname);
  ArgSymbol* lifnTag = new ArgSymbol(INTENT_PARAM, "tag", dtUnknown,
                                     new CallExpr(PRIM_TYPEOF, tag));
  lifn->insertFormalAtTail(lifnTag);
  lifn->where = new BlockStmt(new CallExpr("==", lifnTag, tag->copy()));
  fn->insertAtHead(new DefExpr(lifn));
  VarSymbol* leaderIterator = newTemp("_leaderIterator");
  leaderIterator->addFlag(FLAG_EXPR_TEMP);
  lifn->insertAtTail(new DefExpr(leaderIterator));
  lifn->insertAtTail(new CallExpr(PRIM_MOVE, leaderIterator, new CallExpr("_toLeader", lifnIterator)));
  lifn->insertAtTail(new CallExpr(PRIM_RETURN, leaderIterator));

  //
  // build follower iterator function
  //
  FnSymbol* fifn = new FnSymbol(iteratorName);
  fifn->addFlag(FLAG_PROC_ITER_KW_USED); // ProcIter: remove
  fifn->addFlag(FLAG_ITERATOR_FN); // ProcIter: I think we should keep this one
  ArgSymbol* fifnIterator = new ArgSymbol(INTENT_BLANK, "iterator", dtAny);
  fifn->insertFormalAtTail(fifnIterator);

  tag = buildDotExpr(buildDotExpr(new UnresolvedSymExpr("ChapelBase"),
                                  iterKindTypename), iterKindFollowerTagname);
  ArgSymbol* fifnTag = new ArgSymbol(INTENT_PARAM, "tag", dtUnknown,
                                     new CallExpr(PRIM_TYPEOF, tag));
  fifn->insertFormalAtTail(fifnTag);
  ArgSymbol* fifnFollower = new ArgSymbol(INTENT_BLANK, iterFollowthisArgname, dtAny);
  fifn->insertFormalAtTail(fifnFollower);
  fifn->where = new BlockStmt(new CallExpr("==", fifnTag, tag->copy()));
  fn->insertAtHead(new DefExpr(fifn));
  VarSymbol* followerIterator = newTemp("_followerIterator");
  followerIterator->addFlag(FLAG_EXPR_TEMP);
  fifn->insertAtTail(new DefExpr(followerIterator));
  fifn->insertAtTail(new CallExpr(PRIM_MOVE, followerIterator, new CallExpr("_toFollower", fifnIterator, fifnFollower)));
  // do we need to use this map since symbols have not been resolved?
  SymbolMap map;
  Expr* indicesCopy = (indices) ? indices->copy(&map) : NULL;
  Expr* bodyCopy = stmt->copy(&map);
  fifn->insertAtTail(buildForLoopStmt(indicesCopy, new SymExpr(followerIterator), new BlockStmt(bodyCopy)));
  return new CallExpr(new DefExpr(fn));
}


BlockStmt* buildForLoopStmt(Expr* indices,
                            Expr* iteratorExpr,
                            BlockStmt* body,
                            bool coforall) {
  //
  // insert temporary index when elided by user
  //
  if (!indices)
    indices = new UnresolvedSymExpr("chpl__elidedIdx");

  checkIndices(indices);

  body = new BlockStmt(body);
  BlockStmt* stmts = buildChapelStmt();
  LabelSymbol* continueLabel = new LabelSymbol("_continueLabel");
  continueLabel->addFlag(FLAG_TEMP);
  continueLabel->addFlag(FLAG_LABEL_CONTINUE);
  body->continueLabel = continueLabel;
  LabelSymbol* breakLabel = new LabelSymbol("_breakLabel");
  breakLabel->addFlag(FLAG_TEMP);
  breakLabel->addFlag(FLAG_LABEL_BREAK);
  body->breakLabel = breakLabel;

  VarSymbol* iterator = newTemp("_iterator");
  iterator->addFlag(FLAG_EXPR_TEMP);
  stmts->insertAtTail(new DefExpr(iterator));
  stmts->insertAtTail(new CallExpr(PRIM_MOVE, iterator, new CallExpr("_getIterator", iteratorExpr)));
  VarSymbol* index = newTemp("_indexOfInterest");
  stmts->insertAtTail(new DefExpr(index));
  stmts->insertAtTail(new BlockStmt(
    new CallExpr(PRIM_MOVE, index,
      new CallExpr("iteratorIndex", iterator)),
    BLOCK_TYPE));
  destructureIndices(body, indices, new SymExpr(index), coforall);
  body->blockInfo = new CallExpr(PRIM_BLOCK_FOR_LOOP, index, iterator);

  body->insertAtTail(new DefExpr(continueLabel));
  stmts->insertAtTail(body);
  stmts->insertAtTail(new DefExpr(breakLabel));
  stmts->insertAtTail(new CallExpr("_freeIterator", iterator));
  return stmts;
}


static BlockStmt*
buildFollowLoop(Symbol* iter, Symbol* leadIdxCopy, Symbol* followIter,
                Symbol* followIdx, Expr* indices, BlockStmt* loopBody,
                bool fast) {
  BlockStmt* followBlock = new BlockStmt();
  followBlock->insertAtTail(new DefExpr(followIter));
  if (fast)
    followBlock->insertAtTail("'move'(%S, _getIterator(_toFastFollower(%S, %S)))", followIter, iter, leadIdxCopy);
  else
    followBlock->insertAtTail("'move'(%S, _getIterator(_toFollower(%S, %S)))", followIter, iter, leadIdxCopy);
  followBlock->insertAtTail(new DefExpr(followIdx));
  followBlock->insertAtTail("{TYPE 'move'(%S, iteratorIndex(%S)) }", followIdx, followIter);
  BlockStmt* followBody = new BlockStmt();
  followBody->insertAtTail(loopBody);
  destructureIndices(followBody, indices, new SymExpr(followIdx), false);
  followBody->blockInfo = new CallExpr(PRIM_BLOCK_FOR_LOOP, followIdx, followIter);
  followBlock->insertAtTail(followBody);
  followBlock->insertAtTail(new CallExpr("_freeIterator", followIter));
  return followBlock;
}


BlockStmt*
buildForallLoopStmt(Expr* indices, Expr* iterExpr, BlockStmt* loopBody) {
  checkControlFlow(loopBody, "forall statement");

  if (fSerial || fSerialForall)
    return buildForLoopStmt(indices, iterExpr, loopBody);

  //
  // insert temporary index when elided by user
  //
  if (!indices)
    indices = new UnresolvedSymExpr("chpl__elidedIdx");

  checkIndices(indices);

  VarSymbol* iter = newTemp("chpl__iter");
  VarSymbol* leadIdx = newTemp("chpl__leadIdx");
  VarSymbol* leadIter = newTemp("chpl__leadIter");
  VarSymbol* leadIdxCopy = newTemp("chpl__leadIdxCopy");
  VarSymbol* fastFollowIdx = newTemp("chpl__fastFollowIdx");
  VarSymbol* fastFollowIter = newTemp("chpl__fastFollowIter");
  VarSymbol* followIdx = newTemp("chpl__followIdx");
  VarSymbol* followIter = newTemp("chpl__followIter");
  iter->addFlag(FLAG_EXPR_TEMP);
  leadIdxCopy->addFlag(FLAG_INDEX_VAR);
  leadIdxCopy->addFlag(FLAG_INSERT_AUTO_DESTROY);

  Symbol* T1 = newTemp(); T1->addFlag(FLAG_EXPR_TEMP); T1->addFlag(FLAG_MAYBE_PARAM);
  Symbol* T2 = newTemp(); T2->addFlag(FLAG_EXPR_TEMP); T2->addFlag(FLAG_MAYBE_PARAM);

  BlockStmt* leadBlock = buildChapelStmt();
  leadBlock->insertAtTail(new DefExpr(iter));
  leadBlock->insertAtTail(new DefExpr(leadIdx));
  leadBlock->insertAtTail(new DefExpr(leadIter));
  leadBlock->insertAtTail("'move'(%S, _checkIterator(%E))",
                          iter, iterExpr);
  leadBlock->insertAtTail("'move'(%S, _getIterator(_toLeader(%S)))",
                          leadIter, iter);
  leadBlock->insertAtTail("{TYPE 'move'(%S, iteratorIndex(%S)) }",
                          leadIdx, leadIter);
  BlockStmt* leadBody = new BlockStmt();
  leadBody->insertAtTail(new DefExpr(leadIdxCopy));
  leadBody->insertAtTail("'move'(%S, %S)", leadIdxCopy, leadIdx);
  BlockStmt* followBlock = buildFollowLoop(iter, leadIdxCopy, followIter, followIdx, indices, loopBody->copy(), false);
  if (!fNoFastFollowers) {
    leadBody->insertAtTail(new DefExpr(T1));
    leadBody->insertAtTail(new DefExpr(T2));
    leadBody->insertAtTail("'move'(%S, chpl__staticFastFollowCheck(%S))", T1, iter);
    leadBody->insertAtTail(new CondStmt(new SymExpr(T1),
                             new_Expr("'move'(%S, chpl__dynamicFastFollowCheck(%S))", T2, iter),
                             new_Expr("'move'(%S, %S)", T2, gFalse)));
    BlockStmt* fastFollowBlock = buildFollowLoop(iter, leadIdxCopy, fastFollowIter, fastFollowIdx, indices, loopBody, true);
    leadBody->insertAtTail(new CondStmt(new SymExpr(T2), fastFollowBlock, followBlock));
  } else {
    leadBody->insertAtTail(followBlock);
  }
  leadBody->blockInfo = new CallExpr(PRIM_BLOCK_FOR_LOOP, leadIdx, leadIter);
  leadBlock->insertAtTail(leadBody);
  leadBlock->insertAtTail("_freeIterator(%S)", leadIter);

  return leadBlock;
}


BlockStmt* buildCoforallLoopStmt(Expr* indices, Expr* iterator, BlockStmt* body) {
  checkControlFlow(body, "coforall statement");

  if (fSerial)
    return buildForLoopStmt(indices, iterator, body);

  //
  // insert temporary index when elided by user
  //
  if (!indices)
    indices = new UnresolvedSymExpr("chpl__elidedIdx");

  checkIndices(indices);

  //
  // detect on-statement directly inside coforall-loop
  //
  BlockStmt* onBlock = NULL;
  BlockStmt* tmp = body;
  while (tmp) {
    if (BlockStmt* b = toBlockStmt(tmp->body.tail)) {
      if (b->blockInfo && b->blockInfo->isPrimitive(PRIM_BLOCK_ON)) {
        onBlock = b;
        break;
      }
    }
    if (tmp->body.tail == tmp->body.head) {
      tmp = toBlockStmt(tmp->body.tail);
      if (tmp && tmp->blockInfo)
        tmp = NULL;
    } else
      tmp = NULL;
  }

  if (onBlock) {
    //
    // optimization of on-statements directly inside coforall-loops
    //
    //   In this case, the on-statement is made into a non-blocking
    //   on-statement and the coforall is serialized (rather than
    //   wasting threads that would do nothing other than wait on the
    //   on-statement.
    //
    VarSymbol* coforallCount = newTemp("_coforallCount");
    BlockStmt* block = buildForLoopStmt(indices, iterator, body, true);
    block->insertAtHead(new CallExpr(PRIM_MOVE, coforallCount, new CallExpr("_endCountAlloc")));
    block->insertAtHead(new DefExpr(coforallCount));
    body->insertAtHead(new CallExpr("_upEndCount", coforallCount));
    block->insertAtTail(new CallExpr("_waitEndCount", coforallCount));
    block->insertAtTail(new CallExpr("_endCountFree", coforallCount));
    onBlock->blockInfo->primitive = primitives[PRIM_BLOCK_ON_NB];
    BlockStmt* innerOnBlock = new BlockStmt();
    for_alist(tmp, onBlock->body) {
      innerOnBlock->insertAtTail(tmp->remove());
    }
    onBlock->insertAtHead(innerOnBlock);
    onBlock->insertAtTail(new CallExpr("_downEndCount", coforallCount));
    return block;
  } else {
    VarSymbol* coforallCount = newTemp("_coforallCount");
    BlockStmt* beginBlk = new BlockStmt();
    beginBlk->blockInfo = new CallExpr(PRIM_BLOCK_COFORALL);
    beginBlk->insertAtHead(body);
    beginBlk->insertAtTail(new CallExpr("_downEndCount", coforallCount));
    BlockStmt* block = buildForLoopStmt(indices, iterator, beginBlk, true);
    block->insertAtHead(new CallExpr(PRIM_MOVE, coforallCount, new CallExpr("_endCountAlloc")));
    block->insertAtHead(new DefExpr(coforallCount));
    block->insertAtTail(new CallExpr(PRIM_PROCESS_TASK_LIST, coforallCount));
    beginBlk->insertBefore(new CallExpr("_upEndCount", coforallCount));
    block->insertAtTail(new CallExpr("_waitEndCount", coforallCount));
    block->insertAtTail(new CallExpr("_endCountFree", coforallCount));
    return block;
  }
}


static Symbol*
insertBeforeCompilerTemp(Expr* stmt, Expr* expr) {
  Symbol* expr_var = newTemp();
  expr_var->addFlag(FLAG_MAYBE_PARAM);
  stmt->insertBefore(new DefExpr(expr_var));
  stmt->insertBefore(new CallExpr(PRIM_MOVE, expr_var, expr));
  return expr_var;
}


BlockStmt* buildParamForLoopStmt(const char* index, Expr* range, BlockStmt* stmts) {
  BlockStmt* block = new BlockStmt(stmts);
  BlockStmt* outer = new BlockStmt(block);
  VarSymbol* indexVar = new VarSymbol(index);
  block->insertBefore(new DefExpr(indexVar, new_IntSymbol((int64_t)0)));
  Expr *low = NULL, *high = NULL, *stride;
  CallExpr* call = toCallExpr(range);
  if (call && call->isNamed("by")) {
    stride = call->get(2)->remove();
    call = toCallExpr(call->get(1));
  } else {
    stride = new SymExpr(new_IntSymbol(1));
  }
  if (call && call->isNamed("_build_range")) {
    low = call->get(1)->remove();
    high = call->get(1)->remove();
  } else
    USR_FATAL(range, "iterators for param-for-loops must be literal ranges");
  Symbol* lowVar = insertBeforeCompilerTemp(block, low);
  Symbol* highVar = insertBeforeCompilerTemp(block, high);
  Symbol* strideVar = insertBeforeCompilerTemp(block, stride);
  block->blockInfo = new CallExpr(PRIM_BLOCK_PARAM_LOOP, indexVar, lowVar, highVar, strideVar);
  return buildChapelStmt(outer);
}


BlockStmt*
buildAssignment(Expr* lhs, Expr* rhs, const char* op) {
  if (op == NULL)
    return buildChapelStmt(new CallExpr("=", lhs, rhs));

  BlockStmt* stmt = buildChapelStmt();

  VarSymbol* ltmp = newTemp();
  ltmp->addFlag(FLAG_MAYBE_PARAM);
  stmt->insertAtTail(new DefExpr(ltmp));
  stmt->insertAtTail(new CallExpr(PRIM_MOVE, ltmp,
                       new CallExpr(PRIM_SET_REF, lhs)));

  VarSymbol* rtmp = newTemp();
  rtmp->addFlag(FLAG_MAYBE_PARAM);
  rtmp->addFlag(FLAG_EXPR_TEMP);
  stmt->insertAtTail(new DefExpr(rtmp));
  stmt->insertAtTail(new CallExpr(PRIM_MOVE, rtmp, rhs));

  BlockStmt* cast =
    new BlockStmt(
      new CallExpr("=", ltmp,
        new CallExpr("_cast",
          new CallExpr(PRIM_TYPEOF, ltmp),
          new CallExpr(op,
            new CallExpr(PRIM_GET_REF, ltmp), rtmp))));

  if (strcmp(op, "<<") && strcmp(op, ">>"))
    cast->insertAtHead(
      new BlockStmt(new CallExpr("=", ltmp, rtmp), BLOCK_TYPE));

  CondStmt* inner =
    new CondStmt(
      new CallExpr("_isPrimitiveType",
        new CallExpr(PRIM_TYPEOF,
          new CallExpr(PRIM_GET_REF, ltmp))),
      cast,
      new CallExpr("=", ltmp,
        new CallExpr(op,
          new CallExpr(PRIM_GET_REF, ltmp), rtmp)));

  // This code performs a rewrite of += and -= for domains at
  // compile-time.
  //     D += x     becomes     D.add(x)
  //     D -= x     becomes     D.remove(x)
  // Even though we can handle this in the module code (ChapelArray.chpl)
  // because we overload +/- and =, we choose to rewrite the expression
  // because using the assignment operator results in an O(n) operation
  // rather than O(1) (see overload of = for domains in ChapelArray.chpl).
  //
  // The right way to do this would be to make += and += proper methods
  // that can be overloaded.  When we get around to this, this rewrite
  // should be removed.
  if (!strcmp(op, "+")) {
    stmt->insertAtTail(
      new CondStmt(
        new CallExpr("chpl__isDomain", ltmp),
        new CallExpr(
          new CallExpr(".", ltmp, new_StringSymbol("add")), rtmp),
        inner));
  } else if (!strcmp(op, "-")) {
    stmt->insertAtTail(
      new CondStmt(
        new CallExpr("chpl__isDomain", ltmp),
        new CallExpr(
          new CallExpr(".", ltmp, new_StringSymbol("remove")), rtmp),
        inner));
  } else {
    stmt->insertAtTail(inner);
  }

  return stmt;
}


BlockStmt* buildLAndAssignment(Expr* lhs, Expr* rhs) {
  BlockStmt* stmt = buildChapelStmt();
  VarSymbol* ltmp = newTemp();
  stmt->insertAtTail(new DefExpr(ltmp));
  stmt->insertAtTail(new CallExpr(PRIM_MOVE, ltmp, new CallExpr(PRIM_SET_REF, lhs)));
  stmt->insertAtTail(new CallExpr("=", ltmp, buildLogicalAndExpr(ltmp, rhs)));
  return stmt;
}


BlockStmt* buildLOrAssignment(Expr* lhs, Expr* rhs) {
  BlockStmt* stmt = buildChapelStmt();
  VarSymbol* ltmp = newTemp();
  stmt->insertAtTail(new DefExpr(ltmp));
  stmt->insertAtTail(new CallExpr(PRIM_MOVE, ltmp, new CallExpr(PRIM_SET_REF, lhs)));
  stmt->insertAtTail(new CallExpr("=", ltmp, buildLogicalOrExpr(ltmp, rhs)));
  return stmt;
}

BlockStmt* buildSwapStmt(Expr* lhs, Expr* rhs) {
  return buildChapelStmt(new CallExpr("_chpl_swap", lhs, rhs));
}

BlockStmt* buildSelectStmt(Expr* selectCond, BlockStmt* whenstmts) {
  CondStmt* otherwise = NULL;
  CondStmt* top = NULL;
  CondStmt* condStmt = NULL;

  for_alist(stmt, whenstmts->body) {
    CondStmt* when = toCondStmt(stmt);
    if (!when)
      INT_FATAL("error in buildSelectStmt");
    CallExpr* conds = toCallExpr(when->condExpr);
    if (!conds || !conds->isPrimitive(PRIM_WHEN))
      INT_FATAL("error in buildSelectStmt");
    if (conds->numActuals() == 0) {
      if (otherwise)
        USR_FATAL(selectCond, "Select has multiple otherwise clauses");
      otherwise = when;
    } else {
      Expr* expr = NULL;
      for_actuals(whenCond, conds) {
        whenCond->remove();
        if (!expr)
          expr = new CallExpr("==", selectCond->copy(), whenCond);
        else
          expr = buildLogicalOrExpr(expr, new CallExpr("==",
                                                   selectCond->copy(),
                                                   whenCond));
      }
      if (!condStmt) {
        condStmt = new CondStmt(new CallExpr("_cond_test", expr), when->thenStmt);
        top = condStmt;
      } else {
        CondStmt* next = new CondStmt(new CallExpr("_cond_test", expr), when->thenStmt);
        condStmt->elseStmt = new BlockStmt(next);
        condStmt = next;
      }
    }
  }
  if (otherwise) {
    if (!condStmt)
      USR_FATAL(selectCond, "Select has no when clauses");
    condStmt->elseStmt = otherwise->thenStmt;
  }
  return buildChapelStmt(top);
}


BlockStmt* buildTypeSelectStmt(CallExpr* exprs, BlockStmt* whenstmts) {
  static int uid = 1;
  int caseId = 1;
  FnSymbol* fn = NULL;
  BlockStmt* stmts = buildChapelStmt();
  BlockStmt* newWhenStmts = buildChapelStmt();
  bool has_otherwise = false;

  INT_ASSERT(exprs->isPrimitive(PRIM_ACTUALS_LIST));

  for_alist(stmt, whenstmts->body) {
    CondStmt* when = toCondStmt(stmt);
    if (!when)
      INT_FATAL("error in buildSelectStmt");
    CallExpr* conds = toCallExpr(when->condExpr);
    if (!conds || !conds->isPrimitive(PRIM_WHEN))
      INT_FATAL("error in buildSelectStmt");
    if (conds->numActuals() == 0) {
      if (has_otherwise)
        USR_FATAL(conds, "Type select statement has multiple otherwise clauses");
      has_otherwise = true;
      fn = new FnSymbol(astr("_typeselect", istr(uid)));
      fn->addFlag(FLAG_PROC_ITER_KW_USED); // ProcIter: remove
      int lid = 1;
      for_actuals(expr, exprs) {
        fn->insertFormalAtTail(
          new DefExpr(
            new ArgSymbol(INTENT_BLANK,
                          astr("_t", istr(lid++)),
                          dtAny)));
      }
      fn->retTag = RET_PARAM;
      fn->insertAtTail(new CallExpr(PRIM_RETURN, new_IntSymbol(caseId)));
      newWhenStmts->insertAtTail(
        new CondStmt(new CallExpr(PRIM_WHEN, new_IntSymbol(caseId++)),
        when->thenStmt->copy()));
      stmts->insertAtTail(new DefExpr(fn));
    } else {
      if (conds->numActuals() != exprs->argList.length)
        USR_FATAL(when, "Type select statement requires number of selectors to be equal to number of when conditions");
      fn = new FnSymbol(astr("_typeselect", istr(uid)));
      fn->addFlag(FLAG_PROC_ITER_KW_USED); // ProcIter: remove
      int lid = 1;
      for_actuals(expr, conds) {
        fn->insertFormalAtTail(
          new DefExpr(new ArgSymbol(INTENT_BLANK, astr("_t", istr(lid++)),
                                    dtUnknown, expr->copy())));
      }
      fn->retTag = RET_PARAM;
      fn->insertAtTail(new CallExpr(PRIM_RETURN, new_IntSymbol(caseId)));
      newWhenStmts->insertAtTail(
        new CondStmt(new CallExpr(PRIM_WHEN, new_IntSymbol(caseId++)),
        when->thenStmt->copy()));
      stmts->insertAtTail(new DefExpr(fn));
    }
  }
  VarSymbol* tmp = newTemp();
  tmp->addFlag(FLAG_MAYBE_PARAM);
  stmts->insertAtHead(new DefExpr(tmp));
  stmts->insertAtTail(new CallExpr(PRIM_MOVE,
                                   tmp,
                                   new CallExpr(fn->name, exprs)));
  stmts->insertAtTail(buildSelectStmt(new SymExpr(tmp), newWhenStmts));
  return stmts;
}


static void
buildReduceScanPreface(FnSymbol* fn, Symbol* data, Symbol* eltType, Symbol* globalOp,
                       Expr* opExpr, Expr* dataExpr) {
  if (UnresolvedSymExpr* sym = toUnresolvedSymExpr(opExpr)) {
    if (!strcmp(sym->unresolved, "max"))
      sym->unresolved = astr("MaxReduceScanOp");
    else if (!strcmp(sym->unresolved, "min"))
      sym->unresolved = astr("MinReduceScanOp");
  }

  data->addFlag(FLAG_EXPR_TEMP);
  eltType->addFlag(FLAG_MAYBE_TYPE);

  fn->insertAtTail(new DefExpr(data));
  fn->insertAtTail(new DefExpr(eltType));
  fn->insertAtTail(new DefExpr(globalOp));
  fn->insertAtTail("'move'(%S, %E)", data, dataExpr);
  fn->insertAtTail("{TYPE 'move'(%S, 'typeof'(chpl__initCopy(iteratorIndex(_getIterator(%S)))))}", eltType, data);
  fn->insertAtTail("'move'(%S, 'new'(%E(%E)))", globalOp, opExpr, new NamedExpr("eltType", new SymExpr(eltType)));
}


CallExpr* buildReduceExpr(Expr* opExpr, Expr* dataExpr) {
  static int uid = 1;

  FnSymbol* fn = new FnSymbol(astr("chpl__reduce", istr(uid++)));
  fn->addFlag(FLAG_COMPILER_NESTED_FUNCTION);
  fn->addFlag(FLAG_DONT_DISABLE_REMOTE_VALUE_FORWARDING);
  fn->addFlag(FLAG_INLINE);

  VarSymbol* data = newTemp();
  VarSymbol* eltType = newTemp();
  VarSymbol* globalOp = newTemp();

  buildReduceScanPreface(fn, data, eltType, globalOp, opExpr, dataExpr);

  BlockStmt* serialBlock = buildChapelStmt();
  VarSymbol* index = newTemp("_index");
  serialBlock->insertAtTail(new DefExpr(index));
  serialBlock->insertAtTail(buildForLoopStmt(new SymExpr(index), new SymExpr(data), new BlockStmt(new CallExpr(new CallExpr(".", globalOp, new_StringSymbol("accumulate")), index))));

  if (fSerial || fSerialForall) {
    fn->insertAtTail(serialBlock);
  } else {
    VarSymbol* leadIdx = newTemp("chpl__leadIdx");
    VarSymbol* leadIter = newTemp("chpl__leadIter");
    VarSymbol* leadIdxCopy = newTemp("chpl__leadIdxCopy");
    VarSymbol* followIdx = newTemp("chpl__followIdx");
    VarSymbol* followIter = newTemp("chpl__followIter");
    VarSymbol* localOp = newTemp();
    leadIdxCopy->addFlag(FLAG_INDEX_VAR);
    leadIdxCopy->addFlag(FLAG_INSERT_AUTO_DESTROY);
    BlockStmt* followBody = new BlockStmt();
    followBody->insertAtTail(".(%S, 'accumulate')(%S)", localOp, followIdx);
    followBody->blockInfo = new CallExpr(PRIM_BLOCK_FOR_LOOP, followIdx, followIter);
    BlockStmt* followBlock = new BlockStmt();
    followBlock->insertAtTail(new DefExpr(followIter));
    followBlock->insertAtTail(new DefExpr(followIdx));
    followBlock->insertAtTail(new DefExpr(localOp));
    followBlock->insertAtTail("'move'(%S, _getIterator(_toFollower(%S, %S)))", followIter, data, leadIdxCopy);
    followBlock->insertAtTail("{TYPE 'move'(%S, iteratorIndex(%S))}", followIdx, followIter);
    followBlock->insertAtTail("'move'(%S, 'new'(%E(%E)))", localOp, opExpr->copy(), new NamedExpr("eltType", new SymExpr(eltType)));
    followBlock->insertAtTail(followBody);
    followBlock->insertAtTail("chpl__reduceCombine(%S, %S)", globalOp, localOp);
    followBlock->insertAtTail("'delete'(%S)", localOp);
    followBlock->insertAtTail("_freeIterator(%S)", followIter);
    BlockStmt* leadBody = new BlockStmt();
    leadBody->insertAtTail(new DefExpr(leadIdxCopy));
    leadBody->insertAtTail("'move'(%S, %S)", leadIdxCopy, leadIdx);
    leadBody->insertAtTail(followBlock);
    leadBody->blockInfo = new CallExpr(PRIM_BLOCK_FOR_LOOP, leadIdx, leadIter);
    BlockStmt* leadBlock = buildChapelStmt();
    leadBlock->insertAtTail(new DefExpr(leadIdx));
    leadBlock->insertAtTail(new DefExpr(leadIter));
    leadBlock->insertAtTail("'move'(%S, _getIterator(_toLeader(%S)))", leadIter, data);
    leadBlock->insertAtTail("{TYPE 'move'(%S, iteratorIndex(%S))}", leadIdx, leadIter);
    leadBlock->insertAtTail(leadBody);
    leadBlock->insertAtTail("_freeIterator(%S)", leadIter);
    serialBlock->insertAtHead("compilerWarning('reduce has been serialized (see note in $CHPL_HOME/STATUS)')");
    fn->insertAtTail(new CondStmt(new SymExpr(gTryToken), leadBlock, serialBlock));
  }

  VarSymbol* result = new VarSymbol("result");
  fn->insertAtTail(new DefExpr(result, new CallExpr(new CallExpr(".", globalOp, new_StringSymbol("generate")))));
  fn->insertAtTail("'delete'(%S)", globalOp);
  fn->insertAtTail("'return'(%S)", result);
  return new CallExpr(new DefExpr(fn));
}


CallExpr* buildScanExpr(Expr* opExpr, Expr* dataExpr) {
  static int uid = 1;

  FnSymbol* fn = new FnSymbol(astr("chpl__scan", istr(uid++)));
  fn->addFlag(FLAG_COMPILER_NESTED_FUNCTION);

  VarSymbol* data = newTemp();
  VarSymbol* eltType = newTemp();
  VarSymbol* globalOp = newTemp();

  buildReduceScanPreface(fn, data, eltType, globalOp, opExpr, dataExpr);

  if (!fSerial && !fSerialForall)
    fn->insertAtTail("compilerWarning('scan has been serialized (see note in $CHPL_HOME/STATUS)')");

  fn->insertAtTail("'return'(chpl__scanIterator(%S, %S))", globalOp, data);

  return new CallExpr(new DefExpr(fn));
}


static void
backPropagateInitsTypes(BlockStmt* stmts) {
  Expr* init = NULL;
  Expr* type = NULL;
  DefExpr* last = NULL;
  for_alist_backward(stmt, stmts->body) {
    if (DefExpr* def = toDefExpr(stmt)) {
      if (def->init || def->exprType) {
        init = def->init;
        type = def->exprType;
      } else {
        if (type)
          last->exprType =
            new CallExpr(PRIM_TYPEOF, new UnresolvedSymExpr(def->sym->name));
        if (init && type)
          last->init =
            new CallExpr("chpl__readXX", new UnresolvedSymExpr(def->sym->name));
        else if (init && !type)
          last->init = new UnresolvedSymExpr(def->sym->name);
        def->init = init;
        def->exprType = type;
      }
      last = def;
    } else
      INT_FATAL(stmt, "expected DefExpr in backPropagateInitsTypes");
  }
}


BlockStmt*
buildVarDecls(BlockStmt* stmts, Flag externconfig, Flag varconst) {
  for_alist(stmt, stmts->body) {
    if (DefExpr* defExpr = toDefExpr(stmt)) {
      if (VarSymbol* var = toVarSymbol(defExpr->sym)) {
        if (externconfig == FLAG_EXTERN && varconst == FLAG_PARAM)
          USR_FATAL(var, "external params are not supported");

        if (externconfig != FLAG_UNKNOWN)
          var->addFlag(externconfig);
        if (varconst != FLAG_UNKNOWN)
          var->addFlag(varconst);

        if (var->hasFlag(FLAG_CONFIG)) {
          if (Expr *configInit = getCmdLineConfig(var->name)) {
            // config var initialized on the command line
            if (!isUsedCmdLineConfig(var->name)) {
              useCmdLineConfig(var->name);
              // drop the original init expression on the floor
              if (Expr* a = toExpr(configInit))
                defExpr->init = a;
              else if (Symbol* a = toSymbol(configInit))
                defExpr->init = new SymExpr(a);
              else
                INT_FATAL(stmt, "DefExpr initialized with bad exprType config ast");
            } else {
              // name is ambiguous, must specify module name
              USR_FATAL(var, "Ambiguous config param or type name (%s)", var->name);
            }
          }
        }
        continue;
      }
    }
    INT_FATAL(stmt, "Major error in setVarSymbolAttributes");
  }
  backPropagateInitsTypes(stmts);
  //
  // If blockInfo is set, this is a tuple variable declaration.
  // Add checks that the expression on the right is a tuple and that
  // the tuple size matches the number of variables. If not, issue
  // compilerErrors. blockInfo has the form:
  // call("_check_tuple_var_decl", rhsTuple, numVars)
  //
  if (stmts->blockInfo) {
    INT_ASSERT(stmts->blockInfo->isNamed("_check_tuple_var_decl"));
    SymExpr* tuple = toSymExpr(stmts->blockInfo->get(1));
    Expr* varCount = stmts->blockInfo->get(2);
    tuple->var->defPoint->insertAfter(
      buildIfStmt(new CallExpr("!=", new CallExpr(".", tuple->remove(),
                                                  new_StringSymbol("size")),
                               varCount->remove()),
                  new CallExpr("compilerError", new_StringSymbol("tuple size must match the number of grouped variables"), new_IntSymbol(0))));

    tuple->var->defPoint->insertAfter(
      buildIfStmt(new CallExpr("!", new CallExpr("isTuple", tuple->copy())),
                  new CallExpr("compilerError", new_StringSymbol("illegal tuple variable declaration with non-tuple initializer"), new_IntSymbol(0))));
    stmts->blockInfo = NULL;
  }
  return stmts;
}


DefExpr*
buildClassDefExpr(const char* name, Type* type, Expr* inherit, BlockStmt* decls, Flag isExtern) {
  ClassType* ct = toClassType(type);
  INT_ASSERT(ct);
  TypeSymbol* ts = new TypeSymbol(name, ct);
  DefExpr* def = new DefExpr(ts);
  ct->addDeclarations(decls);
  if (isExtern == FLAG_EXTERN || isExtern == FLAG_OLD_EXTERN_KW_USED) {
    ts->addFlag(FLAG_EXTERN);
    if (inherit)
      USR_FATAL_CONT(inherit, "External types do not currently support inheritance");
    if (isExtern == FLAG_OLD_EXTERN_KW_USED)
      USR_WARN(type, "The _extern keyword is deprecated. Use extern (no leading underscore) instead.");
  }
  if (inherit)
    ct->inherits.insertAtTail(inherit);
  return def;
}


DefExpr*
buildArgDefExpr(IntentTag tag, const char* ident, Expr* type, Expr* init, Expr* variable) {
  ArgSymbol* arg = new ArgSymbol(tag, ident, dtUnknown, type, init, variable);
  if (arg->intent == INTENT_TYPE) {
    type = NULL;
    arg->intent = INTENT_BLANK;
    arg->addFlag(FLAG_TYPE_VARIABLE);
    arg->type = dtAny;
  } else if (!type && !init)
    arg->type = dtAny;
  return new DefExpr(arg);
}


//
// create a single argument and store the tuple-grouped args in the
// variable argument list; these will be moved out of the variable
// argument list in the call to destructureTupleGroupedArgs when the
// formals are added to the formals list in the function (in
// buildFunctionFormal)
//
DefExpr*
buildTupleArgDefExpr(IntentTag tag, BlockStmt* tuple, Expr* type, Expr* init) {
  ArgSymbol* arg = new ArgSymbol(tag, "chpl__tuple_arg_temp", dtUnknown,
                                 type, init, tuple);
  arg->addFlag(FLAG_TEMP);
  if (arg->intent != INTENT_BLANK)
    USR_FATAL(tuple, "intents on tuple-grouped arguments are not yet supported");
  if (!type)
    arg->type = dtTuple;
  return new DefExpr(arg);
}


//
// Destructure tuple function arguments.  Add to the function's where
// clause to match the shape of the tuple being destructured.
//
static void
destructureTupleGroupedArgs(FnSymbol* fn, BlockStmt* tuple, Expr* base) {
  int i = 0;
  for_alist(expr, tuple->body) {
    i++;
    if (DefExpr* def = toDefExpr(expr)) {
      def->init = new CallExpr(base->copy(), new_IntSymbol(i));
      if (!strcmp(def->sym->name, "chpl__tuple_blank")) {
        def->remove();
      } else {
        fn->insertAtHead(def->remove());
      }
    } else if (BlockStmt* inner = toBlockStmt(expr)) {
      destructureTupleGroupedArgs(fn, inner, new CallExpr(base->copy(), new_IntSymbol(i)));
    }
  }

  Expr* where =
    buildLogicalAndExpr(
      new CallExpr("isTuple", base->copy()),
      new CallExpr("==", new_IntSymbol(i),
        new CallExpr(".", base->copy(), new_StringSymbol("size"))));

  if (fn->where) {
    where = buildLogicalAndExpr(fn->where->body.head->remove(), where);
    fn->where->body.insertAtHead(where);
  } else {
    fn->where = new BlockStmt(where);
  }
}

FnSymbol* buildLambda(FnSymbol *fn) {
  static int nextId = 0;
  char buffer[100];

  sprintf(buffer, "_chpl_lambda_%i", nextId++);
  char *name = (char *)malloc(strlen(buffer) + 1);

  strcpy(name, buffer);
  
  if (!fn) {
    fn = new FnSymbol(astr(name));
  }
  else {
    fn->name = astr(name);
    fn->cname = fn->name;
  }
  fn->addFlag(FLAG_COMPILER_NESTED_FUNCTION);
  return fn;
}

// Called like:
// buildFunctionDecl($4, $6, $7, $8, $9);
BlockStmt*
buildFunctionDecl(FnSymbol* fn, RetTag optRetTag, Expr* optRetType,
                  Expr* optWhere, BlockStmt* optFnBody)
{
  // This clause can be removed when the old _extern keyword is obsoleted. <hilde>
  if (fn->hasFlag(FLAG_OLD_EXTERN_KW_USED))
    USR_WARN(fn, "The _extern keyword is deprecated. Use extern (no leading underscore) instead.");

  fn->retTag = optRetTag;
  if (optRetTag == RET_VAR)
  {
    if (fn->hasFlag(FLAG_EXTERN))
      USR_FATAL_CONT(fn, "Extern functions cannot be setters.");
    fn->setter = new DefExpr(new ArgSymbol(INTENT_BLANK, "setter", dtBool));
  }

  if (optRetType)
    fn->retExprType = new BlockStmt(optRetType, BLOCK_SCOPELESS);
  else
    if (fn->hasFlag(FLAG_EXTERN))
      fn->retType = dtVoid;

  if (optWhere)
  {
    if (fn->hasFlag(FLAG_EXTERN))
      USR_FATAL_CONT(fn, "Extern functions cannot have where clauses.");
    fn->where = new BlockStmt(optWhere);
  }

  if (optFnBody)
  {
    if (fn->hasFlag(FLAG_EXTERN))
      USR_FATAL_CONT(fn, "Extern functions cannot have a body.");
    fn->insertAtTail(optFnBody);
  }
  else
    // Looks like this flag is redundant with FLAG_EXTERN. <hilde>
    fn->addFlag(FLAG_FUNCTION_PROTOTYPE);

  return buildChapelStmt(new DefExpr(fn));
}


FnSymbol*
buildFunctionFormal(FnSymbol* fn, DefExpr* def) {
  if (!fn)
    fn = new FnSymbol("_");
  if (!def)
    return fn;
  ArgSymbol* arg = toArgSymbol(def->sym);
  INT_ASSERT(arg);
  fn->insertFormalAtTail(def);
  if (!strcmp(arg->name, "chpl__tuple_arg_temp")) {
    destructureTupleGroupedArgs(fn, arg->variableExpr, new SymExpr(arg));
    arg->variableExpr = NULL;
  }
  return fn;
}


BlockStmt* buildLocalStmt(Expr* stmt) {
  BlockStmt* block = buildChapelStmt();

  if (fLocal) {
    block->insertAtTail(stmt);
    return block;
  }

  BlockStmt* localBlock = new BlockStmt(stmt);
  localBlock->blockInfo = new CallExpr(PRIM_BLOCK_LOCAL);
  block->insertAtTail(localBlock);
  return block;
}


static Expr* extractLocaleID(Expr* expr) {
  // If the on <x> expression is a primitive_on_locale_num, we just want
  // to strip off the primitive and have the naked integer value be the
  // locale ID.
  if (CallExpr* call = toCallExpr(expr)) {
    if (call->isPrimitive(PRIM_ON_LOCALE_NUM)) {
      return call->get(1);
    }
  }

  // Otherwise, we need to wrap the expression in a primitive to query
  // the locale ID of the expression
  return new CallExpr(PRIM_GET_LOCALEID, expr);
}


BlockStmt*
buildOnStmt(Expr* expr, Expr* stmt) {
  checkControlFlow(stmt, "on statement");

  /* GPU Case */
  if (CallExpr* call = toCallExpr(expr)) {
    if (call->isPrimitive(PRIM_ON_GPU)) {
      BlockStmt* block = buildChapelStmt();
      BlockStmt* onBlock = new BlockStmt(stmt);
      //Expr *arg1 = call->get(1)->remove(); 
      //Expr *arg2 = call->get(1)->remove(); 
      //onBlock->blockInfo = new CallExpr(PRIM_ON_GPU, arg1, arg2);
      onBlock->blockInfo = call;
      block->insertAtTail(onBlock);
      return block;
    }
  }

  CallExpr* onExpr = new CallExpr(PRIM_GET_REF, extractLocaleID(expr));

  BlockStmt* body = toBlockStmt(stmt);

  //
  // detect begin statement directly inside on-statement
  //
  BlockStmt* beginBlock = NULL;
  BlockStmt* tmp = body;
  while (tmp) {
    if (BlockStmt* b = toBlockStmt(tmp->body.tail)) {
      if (b->blockInfo && b->blockInfo->isPrimitive(PRIM_BLOCK_BEGIN)) {
        beginBlock = b;
        break;
      }
    }
    if (tmp->body.tail == tmp->body.head) {
      tmp = toBlockStmt(tmp->body.tail);
      if (tmp && tmp->blockInfo)
        tmp = NULL;
    } else
      tmp = NULL;
  }

  if (fLocal) {
    BlockStmt* block = new BlockStmt(stmt);
    block->insertAtHead(onExpr); // evaluate the expression for side effects
    return buildChapelStmt(block);
  }

  if (beginBlock) {
    // Execute the construct "on x begin ..." asynchronously.
    Symbol* tmp = newTemp();
    body->insertAtHead(new CallExpr(PRIM_MOVE, tmp, onExpr));
    body->insertAtHead(new DefExpr(tmp));
    beginBlock->blockInfo = new CallExpr(PRIM_BLOCK_ON, tmp);
    beginBlock->blockInfo->primitive = primitives[PRIM_BLOCK_ON_NB];
    return body;
  } else {
    // Otherwise, wait for the "on" statement to complete.
    BlockStmt* block = buildChapelStmt();
    Symbol* tmp = newTemp();
    block->insertAtTail(new DefExpr(tmp));
    block->insertAtTail(new CallExpr(PRIM_MOVE, tmp, onExpr));
    BlockStmt* onBlock = new BlockStmt(stmt);
    onBlock->blockInfo = new CallExpr(PRIM_BLOCK_ON, tmp);
    block->insertAtTail(onBlock);
    return block;
  }
}


BlockStmt*
buildBeginStmt(Expr* stmt) {
  checkControlFlow(stmt, "begin statement");

  if (fSerial)
    return buildChapelStmt(new BlockStmt(stmt));

  BlockStmt* body = toBlockStmt(stmt);
  
  //
  // detect on-statement directly inside begin statement
  //
  BlockStmt* onBlock = NULL;
  BlockStmt* tmp = body;
  while (tmp) {
    if (BlockStmt* b = toBlockStmt(tmp->body.tail)) {
      if (b->blockInfo && b->blockInfo->isPrimitive(PRIM_BLOCK_ON)) {
        onBlock = b;
        break;
      }
    }
    if (tmp->body.tail == tmp->body.head) {
      tmp = toBlockStmt(tmp->body.tail);
      if (tmp && tmp->blockInfo)
        tmp = NULL;
    } else
      tmp = NULL;
  }

  if (onBlock) {
    body->insertAtHead(new CallExpr("_upEndCount"));
    onBlock->insertAtTail(new CallExpr("_downEndCount"));
    onBlock->blockInfo->primitive = primitives[PRIM_BLOCK_ON_NB];
    return body;
  } else {
    BlockStmt* block = buildChapelStmt();
    block->insertAtTail(new CallExpr("_upEndCount"));
    BlockStmt* beginBlock = new BlockStmt();
    beginBlock->blockInfo = new CallExpr(PRIM_BLOCK_BEGIN);
    beginBlock->insertAtHead(stmt);
    beginBlock->insertAtTail(new CallExpr("_downEndCount"));
    block->insertAtTail(beginBlock);
    return block;
  }
}


BlockStmt*
buildSyncStmt(Expr* stmt) {
  checkControlFlow(stmt, "sync statement");
  if (fSerial)
    return buildChapelStmt(new BlockStmt(stmt));
  BlockStmt* block = new BlockStmt();
  VarSymbol* endCountSave = newTemp("_endCountSave");
  block->insertAtTail(new DefExpr(endCountSave));
  block->insertAtTail(new CallExpr(PRIM_MOVE, endCountSave, new CallExpr(PRIM_GET_END_COUNT)));
  block->insertAtTail(new CallExpr(PRIM_SET_END_COUNT, new CallExpr("_endCountAlloc")));
  block->insertAtTail(stmt);
  block->insertAtTail(new CallExpr("_waitEndCount"));
  block->insertAtTail(new CallExpr("_endCountFree", new CallExpr(PRIM_GET_END_COUNT)));
  block->insertAtTail(new CallExpr(PRIM_SET_END_COUNT, endCountSave));
  return block;
}


BlockStmt*
buildCobeginStmt(BlockStmt* block) {
  BlockStmt* outer = block;

  checkControlFlow(block, "cobegin statement");

  if (block->blockTag == BLOCK_SCOPELESS) {
    block = toBlockStmt(block->body.only());
    INT_ASSERT(block);
    block->remove();
  }

  if (block->length() < 2) {
    USR_WARN(outer, "cobegin has no effect if it contains fewer than 2 statements");
    return buildChapelStmt(block);
  }

  if (fSerial)
    return buildChapelStmt(block);

  VarSymbol* cobeginCount = newTemp("_cobeginCount");
  cobeginCount->addFlag(FLAG_TEMP);

  for_alist(stmt, block->body) {
    BlockStmt* beginBlk = new BlockStmt();
    beginBlk->blockInfo = new CallExpr(PRIM_BLOCK_COBEGIN);
    stmt->insertBefore(beginBlk);
    beginBlk->insertAtHead(stmt->remove());
    beginBlk->insertAtTail(new CallExpr("_downEndCount", cobeginCount));
    block->insertAtHead(new CallExpr("_upEndCount", cobeginCount));
  }

  block->insertAtHead(new CallExpr(PRIM_MOVE, cobeginCount, new CallExpr("_endCountAlloc")));
  block->insertAtHead(new DefExpr(cobeginCount));
  block->insertAtTail(new CallExpr(PRIM_PROCESS_TASK_LIST, cobeginCount));
  block->insertAtTail(new CallExpr("_waitEndCount", cobeginCount));
  block->insertAtTail(new CallExpr("_endCountFree", cobeginCount));

  return block;
}


BlockStmt*
buildGotoStmt(GotoTag tag, const char* name) {
  return buildChapelStmt(new GotoStmt(tag, name));
}

BlockStmt* buildPrimitiveStmt(PrimitiveTag tag, Expr* e1, Expr* e2) {
  return buildChapelStmt(new CallExpr(tag, e1, e2));
}

BlockStmt*
buildAtomicStmt(Expr* stmt) {
  static bool atomic_warning = false;

  if (!atomic_warning) {
    atomic_warning = true;
    USR_WARN(stmt, "atomic keyword is ignored (not implemented)");
  }
  return buildChapelStmt(new BlockStmt(stmt));
}


CallExpr* buildPreDecIncWarning(Expr* expr, char sign) {
  if (sign == '+') {
    USR_WARN(expr, "++ is not a pre-increment");
    return new CallExpr("+", new CallExpr("+", expr));
  } else if (sign == '-') {
    USR_WARN(expr, "-- is not a pre-decrement");
    return new CallExpr("-", new CallExpr("-", expr));
  } else {
    INT_FATAL(expr, "Error in parser");
  }
  return NULL;
}

BlockStmt* convertTypesToExtern(BlockStmt* blk) {
  for_alist(node, blk->body) {
    if (DefExpr* de = toDefExpr(node)) {
      if (!de->init) {
        Symbol* vs = de->sym;
        PrimitiveType* pt = new PrimitiveType(NULL);
        DefExpr* newde = new DefExpr(new TypeSymbol(vs->name, pt));
        de->replace(newde);
        de = newde;
      }           
      de->sym->addFlag(FLAG_EXTERN);
    } else {
      INT_FATAL("Got non-DefExpr in type_alias_decl_stmt");
    }
  }
  return blk;
}

BlockStmt* handleConfigTypes(BlockStmt* blk) {
  for_alist(node, blk->body) {
    if (DefExpr* defExpr = toDefExpr(node)) {
      if (VarSymbol* var = toVarSymbol(defExpr->sym)) {
        var->addFlag(FLAG_CONFIG);
        if (Expr *configInit = getCmdLineConfig(var->name)) {
          // config var initialized on the command line
          if (!isUsedCmdLineConfig(var->name)) {
            useCmdLineConfig(var->name);
            // drop the original init expression on the floor
            if (Expr* a = toExpr(configInit))
              defExpr->init = a;
            else if (Symbol* a = toSymbol(configInit))
              defExpr->init = new SymExpr(a);
            else
              INT_FATAL(node, "Type alias initialized to invalid exprType");
          } else {
            // name is ambiguous, must specify module name
            USR_FATAL(var, "Ambiguous config param or type name (%s)", var->name);
          }
        }
      }
    } else {
      INT_FATAL("Got non-DefExpr in type_alias_decl_stmt");
    }
  }
  return blk;
}
