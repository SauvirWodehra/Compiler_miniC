// irgen.h - lowers the AST to IR and performs semantic checks
// (undeclared names, redeclarations, arity mismatches, break outside loop...).
#pragma once

#include "ast.h"
#include "ir.h"

Module generateIR(const Program& prog);
