/* parser.c
 *
 * Code parser.
 *
 * See https://en.wikipedia.org/wiki/Recursive_descent_parser for
 * an explanation of the setup of the parser.
 *
 * 1995	K.W.E. de Lange
 */
#include <string.h>

#include "expression.h"
#include "identifier.h"
#include "parser.h"
#include "error.h"


/* Forward declarations.
 */
static void block(void);
static void skip_function(void);
static void skip_block(void);
static void function_declaration(void);
static void variable_declaration(objecttype_t type);
static void if_stmnt(void);
static void while_stmnt(void);
static void do_stmnt(void);
static void for_stmnt(void);
static void print_stmnt(void);
static void input_stmnt(void);
static void return_stmt(void);
static void import_stmt(void);
static void expression_stmnt(void);
static ListObject *push_arguments(void);
static void pop_arguments(ListObject *arglist);


static int do_break = 0;		/* Busy quiting loop because of break */
static int do_continue = 0;		/* Busy quiting loop because of continue */


/* Check if the current token matches t. If true then return 1 and read the
 * next token, if false then return 0.
 */
int accept(token_t t)
{
	if (scanner.token == t) {
		scanner.next();
		return 1;
	}
	return 0;
}


/* The current token must match t. If it does then read the next token, if
 * it does not then stop the interpreter with an error message.
 */
int expect(token_t t)
{
	if (accept(t))
		return 1;

	error(SyntaxError, "expected %s instead of %s", \
					   tokenName(t), tokenName(scanner.token));
	return 0;
}


/* Initialise and start the parsing proces.
 */
void parser(void)
{
	function_declaration();

	scanner.next();

	while (1) {
		statement();
		if (accept(ENDMARKER))
			break;
	}
}


/* Store the address (= LPAR after IDENTIFIER) of every function in the module.
 *
 * The identifiers are always placed in the list with local variables.
 */
static void function_declaration(void)
{
	int tmp;
	Identifier *id;

	reader.reset();

	/* surpress debug output when scanning for functions */
	tmp = config.debug;
	config.debug = (config.debug & DEBUGSCANONLY) ? DEBUGTOKEN : 0;

	do {
		if (accept(DEFFUNC)) {
			if (scanner.token != IDENTIFIER)
				error(SyntaxError, "missing identifier after function definition");
			if ((id = identifier.add(scanner.string)) == NULL)
				error(NameError, "%s is allready declared", scanner.string);
			identifier.bind(id, (Object *)reader.save());
			skip_function();
		} else
			scanner.next();
	} while (scanner.token != ENDMARKER);

	config.debug = tmp;

	debug_printf(DEBUGBLOCK, "\n------: %-32s", "Start execution");

	reader.reset();
}


/* Skip interpretation of the statements of a function.
 *
 * in:  token = functions IDENTIFIER
 * out: token = first token after DEDENT at end of function statement-block
 */
static void skip_function(void)
{
	debug_printf(DEBUGBLOCK, "\n------: %-32s%s", "Skip function", scanner.string);

	expect(IDENTIFIER);
	expect(LPAR);

	while (scanner.token != NEWLINE && scanner.token != ENDMARKER)
		scanner.next();

	skip_block();

	debug_printf(DEBUGBLOCK, "\n------: %-32s", "End skip function");
}


/* Skip statements in a block, also considering sub-blocks.
 *
 * in:  token = first token of block; must be NEWLINE, else error
 * out: token = first token after DEDENT at end of block
 *
 */
static void skip_block(void)
{
	int level = 1;

	debug_printf(DEBUGBLOCK, "\n------: %-32s", "Skip block");

	expect(NEWLINE);
	expect(INDENT);

	do {
		scanner.next();
		if (scanner.token == INDENT)
			level++;
		if (scanner.token == DEDENT)
			level--;
	} while (level && scanner.token != ENDMARKER);

	debug_printf(DEBUGBLOCK, "\n------: %-32s", "End skip block");

	scanner.next();
}


/* Statement interpreter.
 *
 * in:  token = token to interpret
 * out: token = first token after statement
 */
void statement(void)
{
	if (accept(DEFCHAR))
		variable_declaration(CHAR_T);
	else if (accept(DEFINT))
		variable_declaration(INT_T);
	else if (accept(DEFFLOAT))
		variable_declaration(FLOAT_T);
	else if (accept(DEFSTR))
		variable_declaration(STR_T);
	else if (accept(DEFLIST))
		variable_declaration(LIST_T);
	else if (accept(DEFFUNC))
		skip_function();
	else if (accept(FOR))
		for_stmnt();
	else if (accept(DO))
		do_stmnt();
	else if (accept(IF))
		if_stmnt();
	else if (accept(IMPORT))
		import_stmt();
	else if (accept(INPUT))
		input_stmnt();
	else if (accept(PASS))
		expect(NEWLINE);
	else if (accept(PRINT))
		print_stmnt();
	else if (accept(RETURN) || accept(DEDENT))
		return_stmt();
	else if (accept(WHILE))
		while_stmnt();
	else if (accept(BREAK))
		do_break = 1;
	else if (accept(CONTINUE))
		do_continue = 1;
	else if (accept(ENDMARKER))
		;
	else
		expression_stmnt();
}


/* Execute a statement-block.
 *
 * Syntax: NEWLINE INDENT statement+ DEDENT
 *
 * in:  token = NEWLINE
 * out: token = DEDENT
 */
static void block(void)
{
	debug_printf(DEBUGBLOCK, "\n------: %-32s", "Start block");

	expect(NEWLINE);
	expect(INDENT);

	while (1) {
		statement();
		if (scanner.token == DEDENT || scanner.token == ENDMARKER)
			break;
		if (do_break || do_continue) {  /* skip rest of block */
			int level = 1;
			do {
				scanner.next();
				if (scanner.token == INDENT)
					level++;
				if (scanner.token == DEDENT)
					level--;
			} while (level && scanner.token != ENDMARKER);
			break;
		}
	}

	debug_printf(DEBUGBLOCK, "\n------: %-32s", "End block");
}


/* Evaluate expression.
 *
 * in:  token = first token of expression
 * out: token = first token after NEWLINE
 */
static void expression_stmnt(void)
{
	Object *obj;

	obj = comma_expr();
	obj_decref(obj);

	expect(NEWLINE);
}


/* Declare variabele(s) and (optionally) assign an initial value.
 *
 * type: variabele(s) type - char, int, float, str, list
 *
 * Syntax: type identifier ( '=' value )? ( ',' identifier ( '=' value )? )* NEWLINE
 *
 * in:  token = first token after DEFCHAR, DEFINT, DEFFLOAT, DEFSTR, DEFLIST
 * out: token = first token after NEWLINE
 */
static void variable_declaration(objecttype_t type)
{
	Identifier *id;
	Object *obj;

	while (1) {
		if (scanner.token != IDENTIFIER)
			error(SyntaxError, "expected identifier instead of %s", \
								tokenName(scanner.token));
		if ((id = identifier.add(scanner.string)) == NULL)
			error(NameError, "identifier %s already declared", scanner.string);

		obj = obj_alloc(type);
		identifier.bind(id, obj);
		scanner.next();

		if (accept(EQUAL)) {
			obj = assignment_expr();
			obj_assign(id->object, obj);
			obj_decref(obj);
		}
		if (accept(NEWLINE))
			break;
		expect(COMMA);
	}
}


/* Evaluate expression and test if result is 0 or <> 0.
 *
 * in:  token = first token of expression
 * out: token = first token after expression (= NEWLINE)
 *
 */
static int condition(void)
{
	int result;
	Object *obj;

	obj = comma_expr();
	result = obj_as_bool(obj);
	obj_decref(obj);

	return result;
}


/* if condition
 *     block
 * else
 *     block
 *
 * in:  token = first token after IF
 * out: token = first token after DEDENT of last statement-block
 */
static void if_stmnt(void)
{
	if (condition()) {
		block();
		expect(DEDENT);
		if (accept(ELSE)) {
			skip_block();
		}
	} else {
		skip_block();
		if (accept(ELSE)) {
			block();
			expect(DEDENT);
		}
	}
}


/* while condition
 *     block
 *
 * in:  token = first token after WHILE
 * out: token = first token after DEDENT of statement-block
 */
static void	while_stmnt(void)
{
	PositionObject *loop;

	do_break = do_continue = 0;

	loop = reader.save();

	while (condition() && !do_break) {
		block();
		do_continue = 0;
		reader.jump(loop);
	}

	do_break = 0;

	skip_block();

	obj_decref(loop);
}


/* do
 *     block
 * while condition NEWLINE
 *
 * in:  token = first token after DO
 * out: token = first token after NEWLINE
 */
static void do_stmnt(void)
{
	PositionObject *loop;

	if (scanner.token != NEWLINE)
		error(SyntaxError, "expected newline after do");

	do_break = do_continue = 0;

	loop = reader.save();

	do {
		reader.jump(loop);
		block();
		do_continue = 0;
		expect(DEDENT);
		expect(WHILE);
	} while	(condition() && !do_break);

	do_break = 0;

	expect(NEWLINE);
	obj_decref(loop);
}


/* Loop through the content of a sequence.
 *
 * for identifier in sequence NEWLINE
 *      block
 *
 * If the identifier does not exist it is created.
 *
 * in:  token = first token after FOR
 * out: token = first token after dedent of block
 */
static void for_stmnt(void)
{
	int_t len;
	Object *sequence;
	Identifier *id = NULL;
	PositionObject *loop;

	if (scanner.token == IDENTIFIER)
		if ((id = identifier.search(scanner.string)) == NULL)
			id = identifier.add(scanner.string);

	expect(IDENTIFIER);
	expect(IN);

	sequence = comma_expr();
	len = obj_length(sequence);

	if (scanner.token != NEWLINE)
		error(SyntaxError, "expected newline");

	do_break = do_continue = 0;

	loop = reader.save();

	for (int_t i = 0; i < len && !do_break; i++) {
		identifier.bind(id, obj_item(sequence, i));
		block();
		identifier.unbind(id);
		do_continue = 0;
		reader.jump(loop);
	}
	do_break = 0;

	skip_block();

	obj_decref(loop);
}


/* Import a module.
 *
 * Syntax: import string ( , string )* NEWLINE
 */
static void import_stmt(void)
{
	PositionObject *pos;
	Object *obj;

	do {
		obj = assignment_expr();
		pos = reader.save();
		reader.import(obj_as_str(obj));
		reader.jump(pos);
		obj_decref(pos);
		obj_decref(obj);
	} while (accept(COMMA));

	expect(NEWLINE);
}


/* Print value(s) naar STDOUT.
 *
 * Syntax: print value ( , value )* NEWLINE
 *
 * in:  token = first token after PRINT
 * out: token = first token after NEWLINE
 */
static void	print_stmnt(void)
{
	Object *obj;

	do {
		obj = assignment_expr();
		debug_printf(~NODEBUG, "\n>>>>>:%-33s%c", " ", '>');
		obj_print(obj);
		obj_decref(obj);
	} while (accept(COMMA));

	expect(NEWLINE);
}


/* Read value(s) from STDIN.
 *
 * Syntax: input string? identifier ( , string? identifier )* NEWLINE
 *
 * in:  token = first token after INPUT
 * out: token = first token after NEWLINE
 */
static void input_stmnt(void)
{
	Identifier *id;
	Object *obj;

	do {
		if (scanner.token == STR) {
			printf("%s", scanner.string);
			scanner.next();
		}
		if (scanner.token != IDENTIFIER)
			error(SyntaxError, "expected identifier instead of %s", \
								tokenName(scanner.token));
		if ((id = identifier.search(scanner.string)) == NULL)
			error(NameError, "identifier %s undeclared", scanner.string);
		obj = obj_scan(TYPE(id->object));
		identifier.bind(id, obj);
		accept(IDENTIFIER);
	} while (accept(COMMA));

	expect(NEWLINE);
}


/* Jump to the function. Reserve local stack for variables. Store the return
 * address in global variable 'return_address'.
 *
 * addr: position in the code of the LPAR of the function definition
 *
 * in:  token = LPAR of argument list
 * out: token = token after RPAR of function call
 */
Object *function_call(PositionObject *addr)
{
	PositionObject *return_to;
	ListObject *arglist;
	jmp_buf temp;
	Object *obj;

	debug_printf(DEBUGBLOCK, "\n------: %-32s", "Start function");

	arglist = push_arguments();
	/* token is now RPAR of function call */
	scope.append_level();

	return_to = reader.save();  /* continue here after return from function */

	reader.jump(addr);  /* jump to function definition */
	expect(IDENTIFIER);

	pop_arguments(arglist);
	expect(RPAR);

	memcpy(&temp, &return_address, sizeof(jmp_buf));
	if (setjmp(return_address) == 0)  /* for return statement */
		block();
	memcpy(&return_address, &temp, sizeof(jmp_buf));

	debug_printf(DEBUGBLOCK, "\n------: %-32s", "End function");

	/* now returned from function */
	if (return_value == NULL)
		return_value = obj_create(INT_T, 0);  /* without return value return integer 0 */

	obj_decref((Object *)arglist);

	reader.jump(return_to);  /* continue at end of function call */
	accept(RPAR);

	scope.remove_level();

	obj_decref((Object *)return_to);
	obj = return_value;
	return_value = NULL;

	return obj;
}


/* Create a list with a deep copy of the object of every function argument.
 *
 * in:  token = function IDENTIFIER
 * out: token = RPAR of argument list in function call
 */
static ListObject *push_arguments(void)
{
	Object *obj;
	ListObject *list;

	list = (ListObject *)obj_alloc(LIST_T);

	expect(LPAR);

	while (scanner.token != RPAR) {
		obj = assignment_expr();
		listnode_append(list, obj_copy(obj));
		obj_decref(obj);
		if (scanner.token == RPAR)
			continue;
		else expect(COMMA);
	}

	return list;
}


/* After a jump to a function read the arguments from 'list' and create local
 * variables. The list has been created via deep copy and so contains new
 * object which only need to be linked to local variable names. Not all
 * arguments have to be read from the list.
 *
 * in:  token = LPAR
 * out: token = RPAR of argument list in function definition
 */
static void pop_arguments(ListObject *list)
{
	Identifier *id;
	Object *obj;

	expect(LPAR);

	while (scanner.token != RPAR) {
		if (scanner.token != IDENTIFIER)
			error(SyntaxError, "expected identifier instead of %s", \
								tokenName(scanner.token));
		if ((id = identifier.add(scanner.string)) == NULL)
			error(NameError, "identifier %s already declared", scanner.string);
		if ((obj = listnode_remove(list, 0)) == NULL)
			error(SyntaxError, "no argument on stack to assign to %s", \
								scanner.string);

		identifier.bind(id, obj);
		expect(IDENTIFIER);
		accept(COMMA);
	}
}


/* return: exit from function with a return value (default int 0).
 *
 * Syntax: return value? NEWLINE
 *
 * in:  token = first token after RETURN
 * out: token = first token after NEWLINE
 */
static void return_stmt(void)
{
	if (scanner.token == NEWLINE)
		return_value = obj_create(INT_T, 0);
	else
		return_value = comma_expr();

	expect(NEWLINE);

	/* When returning from a function jump to function_call(),
	 * else to main() (which will stop the interpreter).
	 */
	longjmp(return_address, 1);
}

