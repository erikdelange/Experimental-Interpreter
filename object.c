/* object.c
 *
 * Operations on objects (variables, functions, ...)
 *
 * Variables and functions are represented as objects. An object contains
 * data but also a number of methods. Every object has minimal and thus
 * mandatory set of methods. This set is: alloc, free, set, vset and print.
 *
 * Which other methods are available depends on the type of the object.
 * See: number.c, str.c, list.c, position.c and none.c. In the current
 * implementation no other methods are defined. Operations on object
 * are called via obj_... functions.
 *
 * Object are created when required, but also automatically removed when
 * no longer needed. For is purpose a reference counter is maintained.
 * Every time an object is allocated or assigned to an identifier the
 * reference counter is incremented. Once a routine no longer needs an
 * object it must decrement the counter. The moment the reference counter
 * hits zero the object is removed from memory. (Beware, if not programmed
 * properly this can be a source of unexplainable bugs or excessive memory
 * consumption).
 *
 * All operations on and between objects are found in object.c and are
 * accessed via function names like obj_... followed by the operation,
 * e.g. obj_add().
 *
 * There are two types of operations: unary and binary. Unary operations
 * require only one operand:
 *
 *  result = operator operand
 *
 * The unary operators are:
 *
 *  -   negation of the operand
 *  +   retuns the operand (so does nothing)
 *  !   logical negation of the operand (returns 0 or 1)
 *
 * Binary operators require two operands:
 *
 *  result = operand1 operator operand2
 *
 *  Artihmetic operators are:   +  -  *  /  %
 *  Comparison operators are:   ==  !=  <>  <  <=  >  >=  in
 *  Logical operators are:      and  or
 *
 * Which operations are supported depends on the object type. Numerical object
 * will support almost everything, lists or strings have less operations.
 *
 * Two operations are only meant for use on list or string objects:
 *
 *  item[index]
 *  slice[start:end]
 *
 * As C functions unary- and binary operations look like:
 *
 *  result *operator(*operand1)
 *  result *operator(*operand1, *operand2)
 *
 *  Examples:
 *
 *  Object *obj_negate(Object *op1)
 *  Object *obj_add(Object *op1, Object *op2)
 *
 * Function arguments operand1 and operand2 - although being pointers - always
 * remain unchanged. Result is a newly created object. Its type is dependent
 * on operand1 and optionally operand2.
 * See function coerce() in number.c for the rules for determining the type
 * of the result for arithmetic operations. For locical and comparison
 * operations the result is always an INTEGER as the language does not have
 * a boolean type.
 *
 * 1994 K.W.E. de Lange
 */
#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "position.h"
#include "number.h"
#include "object.h"
#include "error.h"
#include "none.h"
#include "str.h"


#ifdef DEBUG

static Object *head = NULL;    /* head of doubly linked list with objects */
static Object *tail = NULL;    /* tail of doubly linked list with objects */

static void _enqueue(Object *obj);
static void _dequeue(Object *obj);

#define enqueue(o)  _enqueue(o)
#define dequeue(o)  _dequeue(o)

#else  /* not DEBUG */

#define enqueue(o)  ((void)0)
#define dequeue(o)  ((void)0)

# endif


/* Create a new object of type 'type' and assign the default initial value.
 *
 * The initial refcount of the new object is 1.
 *
 * type     type of the new object
 * return   pointer to new object
 */
Object *obj_alloc(objecttype_t type)
{
	Object *obj = NULL;

	switch (type) {
		case CHAR_T:
			obj = chartype.alloc();
			break;
		case INT_T:
			obj = inttype.alloc();
			break;
		case FLOAT_T:
			obj = floattype.alloc();
			break;
		case STR_T:
			obj = strtype.alloc();
			break;
		case LIST_T:
			obj = listtype.alloc();
			break;
		case LISTNODE_T:
			obj = listnodetype.alloc();
			break;
		case POSITION_T:
			obj = positiontype.alloc();
			break;
		case NONE_T:
			obj = nonetype.alloc();
			break;
		default:
			error(SystemError, "cannot allocate type %d", type);
	}

	if (obj == NULL)
		error(OutOfMemoryError);

	enqueue(obj);

	debug_printf(DEBUGALLOC, "\nalloc : %p %s", (void *)obj, TYPENAME(obj));

	obj_incref(obj);  /* initial refcount = 1 */

	return obj;
}


/* Create a new object of type 'type' and assign an initial value.
 *
 * type     type of the new object, also expected type of the initial value
 * ...      value to assign (mandatory)
 * return   pointer to new object
 */
Object *obj_create(objecttype_t type, ...)
{
	va_list argp;
	Object *obj;

	va_start(argp, type);

	obj = obj_alloc(type);  /* sets refcount to 1 */

	TYPEOBJ(obj)->vset(obj, argp);

	va_end(argp);

	return obj;
}


/* Free the memory which was reserved for an object.
 */
void obj_free(Object *obj)
{
	assert(obj);

	dequeue(obj);

	debug_printf(DEBUGALLOC, "\nfree  : %p %s", (void *)obj, TYPENAME(obj));

	TYPEOBJ(obj)->free(obj);
}


/* Print object value on stdout.
 *
 * obj      pointer to object to print
 */
void obj_print(Object *obj)
{
	assert(obj);

	TYPEOBJ(obj)->print(obj);
	fflush(stdout);
}


/* Read object value from stdin.
 *
 * type     type of the value to read
 * return   new object containing value
 */
Object *obj_scan(objecttype_t type)
{
	char buffer[LINESIZE + 1] = "";
	Object *obj = NULL;

	fgets(buffer, LINESIZE + 1, stdin);
	buffer[strcspn(buffer, "\r\n")] = 0;  /* remove trailing newline */

	switch (type) {
		case CHAR_T:
			obj = obj_create(CHAR_T, str_to_char(buffer));
			break;
		case INT_T:
			obj = obj_create(INT_T, str_to_int(buffer));
			break;
		case FLOAT_T:
			obj = obj_create(FLOAT_T, str_to_float(buffer));
			break;
		case STR_T:
			obj = obj_create(STR_T, buffer);
			break;
		default:
			error(TypeError, "unsupported type for input: %d", type);
	}
	fflush(stdin);

	return obj;
}


/* (type op1)result = op1
 */
Object *obj_copy(Object *op1)
{
	switch (TYPE(op1)) {
		case CHAR_T:
			return obj_create(CHAR_T, obj_as_char(op1));
		case INT_T:
			return obj_create(INT_T, obj_as_int(op1));
		case FLOAT_T:
			return obj_create(FLOAT_T, obj_as_float(op1));
		case STR_T:
			return obj_create(STR_T, obj_as_str(op1));
		case LIST_T:
			return obj_create(LIST_T, obj_as_list(op1));
		case LISTNODE_T:
			return obj_copy(obj_from_listnode(op1));
		default:
			error(TypeError, "cannot copy type %s", TYPENAME(op1));
	}
	return NULL;
}


/* op1 = (type op1) op2
 */
void obj_assign(Object *op1, Object *op2)
{
	Object *obj;

	switch (TYPE(op1)) {
		case CHAR_T:
			TYPEOBJ(op1)->set(op1, obj_as_char(op2));
			break;
		case INT_T:
			TYPEOBJ(op1)->set(op1, obj_as_int(op2));
			break;
		case FLOAT_T:
			TYPEOBJ(op1)->set(op1, obj_as_float(op2));
			break;
		case STR_T:
			obj = obj_to_strobj(op2);
			TYPEOBJ(op1)->set(op1, obj_as_str(obj));
			obj_decref(obj);
			break;
		case LIST_T:
			TYPEOBJ(op1)->set(op1, obj_as_list(op2));
			break;
		case LISTNODE_T:
			TYPEOBJ(op1)->set(op1, obj_copy(op2));
			break;
		default:
			error(TypeError, "unsupported operand type(s) for operation =: %s and %s", \
							  TYPENAME(op1), TYPENAME(op2));
	}
}


/* result = op1 + op2
 */
Object *obj_add(Object *op1, Object *op2)
{
	op1 = isListNode(op1) ? obj_from_listnode(op1) : op1;
	op2 = isListNode(op2) ? obj_from_listnode(op2) : op2;

	if (isNumber(op1) && isNumber(op2))
		return numbertype.add(op1, op2);
	else if (isString(op1) || isString(op2))
		return strtype.concat(op1, op2);
	else if (isList(op1) && isList(op2))
		return listtype.concat((ListObject *)op1, (ListObject *)op2);
	else
		error(TypeError, "unsupported operand type(s) for operation +: %s and %s", \
						  TYPENAME(op1), TYPENAME(op2));
	return NULL;
}


/* result = op1 - op2
 */
Object *obj_sub(Object *op1, Object *op2)
{
	op1 = isListNode(op1) ? obj_from_listnode(op1) : op1;
	op2 = isListNode(op2) ? obj_from_listnode(op2) : op2;

	if (isNumber(op1) && isNumber(op2))
		return numbertype.sub(op1, op2);
	else
		error(TypeError, "unsupported operand type(s) for operation -: %s and %s", \
						  TYPENAME(op1), TYPENAME(op2));
	return NULL;
}


/* result = op1 * op2
 */
Object *obj_mult(Object *op1, Object *op2)
{
	op1 = isListNode(op1) ? obj_from_listnode(op1) : op1;
	op2 = isListNode(op2) ? obj_from_listnode(op2) : op2;

	if (isNumber(op1) && isNumber(op2))
		return numbertype.mul(op1, op2);
	else if ((isNumber(op1) || isNumber(op2)) && (isString(op1) || isString(op2)))
		return strtype.repeat(op1, op2);
	else if ((isNumber(op1) || isNumber(op2)) && (isList(op1) || isList(op2)))
		return listtype.repeat(op1, op2);
	else
		error(TypeError, "unsupported operand type(s) for operation *: %s and %s", \
						  TYPENAME(op1), TYPENAME(op2));
	return NULL;
}


/* result = op1 / op2
 */
Object *obj_divs(Object *op1, Object *op2)
{
	op1 = isListNode(op1) ? obj_from_listnode(op1) : op1;
	op2 = isListNode(op2) ? obj_from_listnode(op2) : op2;

	if (isNumber(op1) && isNumber(op2))
		return numbertype.div(op1, op2);
	else
		error(TypeError, "unsupported operand type(s) for operation /: %s and %s", \
						  TYPENAME(op1), TYPENAME(op2));
	return NULL;
}


/* result = op1 % op2
 */
Object *obj_mod(Object *op1, Object *op2)
{
	op1 = isListNode(op1) ? obj_from_listnode(op1) : op1;
	op2 = isListNode(op2) ? obj_from_listnode(op2) : op2;

	if (isNumber(op1) && isNumber(op2))
		return numbertype.mod(op1, op2);
	else
		error(TypeError, "unsupported operand type(s) for operation %%: %s and %s", \
						  TYPENAME(op1), TYPENAME(op2));
	return NULL;
}


/* result = 0 - op1
 */
Object *obj_invert(Object *op1)
{
	op1 = isListNode(op1) ? obj_from_listnode(op1) : op1;

	if (isNumber(op1))
		return numbertype.inv(op1);
	else
		error(TypeError, "unsupported operand type for operation -: %s", \
						  TYPENAME(op1));
	return NULL;
}


/* result = (int_t)(op1 == op2)
 */
Object *obj_eql(Object *op1, Object *op2)
{
	op1 = isListNode(op1) ? obj_from_listnode(op1) : op1;
	op2 = isListNode(op2) ? obj_from_listnode(op2) : op2;

	if (isNumber(op1) && isNumber(op2))
		return numbertype.eql(op1, op2);
	else if (isString(op1) && isString(op2))
		return strtype.eql(op1, op2);
	else if (isList(op1) && isList(op2))
		return listtype.eql((ListObject *)op1, (ListObject *)op2);
	else
		/* operands of different types are by definition not equal */
		return obj_create(INT_T, (int_t)0);
}


/* result = (int_t)(op1 != op2)
 */
Object *obj_neq(Object *op1, Object *op2)
{
	op1 = isListNode(op1) ? obj_from_listnode(op1) : op1;
	op2 = isListNode(op2) ? obj_from_listnode(op2) : op2;

	if (isNumber(op1) && isNumber(op2))
		return numbertype.neq(op1, op2);
	else if (isString(op1) && isString(op2))
		return strtype.neq(op1, op2);
	else if (isList(op1) && isList(op2))
		return listtype.neq((ListObject *)op1, (ListObject *)op2);
	else
		/* operands of different types are by definition not equal */
		return obj_create(INT_T, (int_t)1);
}


/* result = (int_t)(op1 < op2)
 */
Object *obj_lss(Object *op1, Object *op2)
{
	op1 = isListNode(op1) ? obj_from_listnode(op1) : op1;
	op2 = isListNode(op2) ? obj_from_listnode(op2) : op2;

	if (isNumber(op1) && isNumber(op2))
		return numbertype.lss(op1, op2);
	else
		error(TypeError, "unsupported operand type(s) for operation <: %s and %s", \
						  TYPENAME(op1), TYPENAME(op2));
	return NULL;
}


/* result = (int_t)(op1 <= op2)
 */
Object *obj_leq(Object *op1, Object *op2)
{
	op1 = isListNode(op1) ? obj_from_listnode(op1) : op1;
	op2 = isListNode(op2) ? obj_from_listnode(op2) : op2;

	if (isNumber(op1) && isNumber(op2))
		return numbertype.leq(op1, op2);
	else
		error(TypeError, "unsupported operand type(s) for operation <=: %s and %s", \
						  TYPENAME(op1), TYPENAME(op2));
	return NULL;
}


/* result = (int_t)(op1 > op2)
 */
Object *obj_gtr(Object *op1, Object *op2)
{
	op1 = isListNode(op1) ? obj_from_listnode(op1) : op1;
	op2 = isListNode(op2) ? obj_from_listnode(op2) : op2;

	if (isNumber(op1) && isNumber(op2))
		return numbertype.gtr(op1, op2);
	else
		error(TypeError, "unsupported operand type(s) for operation >: %s and %s", \
						  TYPENAME(op1), TYPENAME(op2));
	return NULL;
}


/* result = (int_t)(op1 >= op2)
 */
Object *obj_geq(Object *op1, Object *op2)
{
	op1 = isListNode(op1) ? obj_from_listnode(op1) : op1;
	op2 = isListNode(op2) ? obj_from_listnode(op2) : op2;

	if (isNumber(op1) && isNumber(op2))
		return numbertype.geq(op1, op2);
	else
		error(TypeError, "unsupported operand type(s) for operation >=: %s and %s", \
						  TYPENAME(op1), TYPENAME(op2));
	return NULL;
}


/* result = (int_t)(op1 or op2)
 */
Object *obj_or(Object *op1, Object *op2)
{
	op1 = isListNode(op1) ? obj_from_listnode(op1) : op1;
	op2 = isListNode(op2) ? obj_from_listnode(op2) : op2;

	if (isNumber(op1) && isNumber(op2))
		return numbertype.or(op1, op2);
	else
		error(TypeError, "unsupported operand type(s) for operation or: %s and %s", \
						  TYPENAME(op1), TYPENAME(op2));
	return NULL;
}


/* result = (int_t)(op1 and op2)
 */
Object *obj_and(Object *op1, Object *op2)
{
	op1 = isListNode(op1) ? obj_from_listnode(op1) : op1;
	op2 = isListNode(op2) ? obj_from_listnode(op2) : op2;

	if (isNumber(op1) && isNumber(op2))
		return numbertype.and(op1, op2);
	else
		error(TypeError, "unsupported operand type(s) for operation and: %s and %s", \
						  TYPENAME(op1), TYPENAME(op2));
	return NULL;
}


/* result = (int_t)(op1 in (sequence)op2)
 */
Object *obj_in(Object *op1, Object *op2)
{
	Object *result = NULL;
	Object *item;
	int_t len;

	op1 = isListNode(op1) ? obj_from_listnode(op1) : op1;
	op2 = isListNode(op2) ? obj_from_listnode(op2) : op2;

	if (isSequence(op2) == 0)
		error(TypeError, "%s is not subscriptable", TYPENAME(op2));

	len = obj_length(op2);

	for (int_t i = 0; i < len; i++) {
		if (result != NULL)
			obj_decref(result);
		item = obj_item(op2, i);
		result = obj_eql(op1, item);
		obj_decref(item);
		if (obj_as_int(result) == 1)
			break;
	}
	return result;
}


/* result = (int_t)!op1
 */
Object *obj_negate(Object *op1)
{
	op1 = isListNode(op1) ? obj_from_listnode(op1) : op1;

	if (isNumber(op1))
		return numbertype.negate(op1);
	else
		error(TypeError, "unsupported operand type for operation !: %s", TYPENAME(op1));

	return NULL;
}


/* item = list[index]
 * item = string[index]
 */
Object *obj_item(Object *sequence, int index)
{
	sequence = isListNode(sequence) ? obj_from_listnode(sequence) : sequence;

	if (TYPE(sequence) == STR_T)
		return (Object *)strtype.item((StrObject *)sequence, index);
	else if (TYPE(sequence) == LIST_T)
		return (Object *)listtype.item((ListObject *)sequence, index);
	else
		error(TypeError, "type %s is not subscriptable", TYPENAME(sequence));

	return NULL;
}


/* slice = list[start:end]
 * slice = string[start:end]
 */
Object *obj_slice(Object *sequence, int start, int end)
{
	sequence = isListNode(sequence) ? obj_from_listnode(sequence) : sequence;

	if (TYPE(sequence) == STR_T)
		return (Object *)strtype.slice((StrObject *)sequence, start, end);
	else if (TYPE(sequence) == LIST_T)
		return (Object *)listtype.slice((ListObject *)sequence, start, end);
	else
		error(TypeError, "type %s is not subscriptable", TYPENAME(sequence));

	return NULL;
}


/* Return number of items in a sequence.
 */
int_t obj_length(Object *sequence)
{
	int_t len;
	Object *obj = NULL;

	sequence = isListNode(sequence) ? obj_from_listnode(sequence) : sequence;

	if (TYPE(sequence) == STR_T)
		obj = strtype.length((StrObject *)sequence);
	else if (TYPE(sequence) == LIST_T)
		obj = listtype.length((ListObject *)sequence);
	else
		error(TypeError, "type %s is not subscriptable", TYPENAME(sequence));

	len = obj_as_int(obj);
	obj_decref(obj);

	return len;
}


/* Return object type as string.
 */
Object *obj_type(Object *op1)
{
	return obj_create(STR_T, TYPENAME(op1));
}


/* Various conversions between variable- and object-types.
 */


/* result = (char_t)op1
 */
char_t obj_as_char(Object *op1)
{
	op1 = isListNode(op1) ? obj_from_listnode(op1) : op1;

	switch (TYPE(op1)) {
		case CHAR_T:
			return (char_t)((CharObject *)op1)->cval;
		case INT_T:
			return (char_t)((IntObject *)op1)->ival;
		case FLOAT_T:
			return (char_t)((FloatObject *)op1)->fval;
		case STR_T:
			return str_to_char(((StrObject *)op1)->sptr);
		default:
			error(ValueError, "cannot convert %s to char", TYPENAME(op1));
	}
	return 0;
}


/* result = (int_t)op1
 */
int_t obj_as_int(Object *op1)
{
	op1 = isListNode(op1) ? obj_from_listnode(op1) : op1;

	switch (TYPE(op1)) {
		case CHAR_T:
			return (int_t)((CharObject *)op1)->cval;
		case INT_T:
			return (int_t)((IntObject *)op1)->ival;
		case FLOAT_T:
			return (int_t)((FloatObject *)op1)->fval;
		case STR_T:
			return str_to_int(((StrObject *)op1)->sptr);
		default:
			error(ValueError, "cannot convert %s to integer", TYPENAME(op1));
	}
	return 0;
}


/* result = (float_t)op1
 */
float_t obj_as_float(Object *op1)
{
	op1 = isListNode(op1) ? obj_from_listnode(op1) : op1;

	switch (TYPE(op1)) {
		case CHAR_T:
			return (float_t)((CharObject *)op1)->cval;
		case INT_T:
			return (float_t)((IntObject *)op1)->ival;
		case FLOAT_T:
			return (float_t)((FloatObject *)op1)->fval;
		case STR_T:
			return str_to_float(((StrObject *)op1)->sptr);
		default:
			error(ValueError, "cannot convert %s to float", TYPENAME(op1));
	}
	return 0;
}


/* result = (str_t)op1
 */
char *obj_as_str(Object *op1)
{
	op1 = isListNode(op1) ? obj_from_listnode(op1) : op1;

	switch (TYPE(op1)) {
		case STR_T:
			return ((StrObject *)op1)->sptr;
		default:
			error(ValueError, "cannot convert %s to string", TYPENAME(op1));
	}
	return NULL;
}


/* result = (list_t)op1
 */
ListObject *obj_as_list(Object *op1)
{
	op1 = isListNode(op1) ? obj_from_listnode(op1) : op1;

	switch(TYPE(op1)) {
		case LIST_T:
			return (ListObject *)op1;
		default:
			error(ValueError, "cannot convert %s to list", TYPENAME(op1));
	}
	return NULL;
}


/* result = (bool_t)op1
 */
bool obj_as_bool(Object *op1)
{
	op1 = isListNode(op1) ? obj_from_listnode(op1) : op1;

	switch (TYPE(op1)) {
		case CHAR_T:
			return obj_as_char(op1) ? true : false;
		case INT_T:
			return obj_as_int(op1) ? true : false;
		case FLOAT_T:
			return obj_as_float(op1) ? true : false;
		default:
			error(ValueError, "cannot convert %s to bool", TYPENAME(op1));
	}
	return false;
}


/* Convert string to a char_t
 */
char_t str_to_char(const char *s)
{
	char_t c = 0;

	if (*s == '\\') {  /* is an escape sequence */
		switch (*++s) {
			case '0' :	c = '\0'; break;
			case 'b' :	c = '\b'; break;
			case 'f' :	c = '\f'; break;
			case 'n' :	c = '\n'; break;
			case 'r' :	c = '\r'; break;
			case 't' :	c = '\t'; break;
			case 'v' :	c = '\v'; break;
			case '\\':	c = '\\'; break;
			case '\'':	c = '\''; break;
			case '\"':	c = '\"'; break;
			default  :	error(ValueError, "unknown escape sequence: %c", *s);
		}
	} else {  /* not an escape sequence */
		if (*s == '\0')
			error(SyntaxError, "empty character constant");
		else
			c = *s;
	}
	if (*++s != '\0')
		error(SyntaxError, "to many characters in character constant");

	return c;
}


/* Convert string to an int_t
 */
int_t str_to_int(const char *s)
{
	char *e;
	int_t i;

	errno = 0;

	i = (int_t)strtol(s, &e, 10);

	if (*e != 0 || errno != 0) {
		if (errno != 0)
			error(ValueError, "cannot convert %s to int; %s", \
							   s, strerror(errno));
		else
			error(ValueError, "cannot convert %s to int", s);
	}
	return i;
}


/* Convert string to a float_t
 */
float_t str_to_float(const char *s)
{
	char *e;
	float_t f;

	errno = 0;

	f = (float_t)strtod(s, &e);

	if (*e != 0 || errno != 0) {
		if (errno != 0)
			error(ValueError, "cannot convert %s to float; %s", \
							   s, strerror(errno));
		else
			error(ValueError, "cannot convert %s to float", s);
	}
	return f;
}


/* Convert object obj to a string object
 */
Object *obj_to_strobj(Object *obj)
{
	char buffer[BUFSIZE+1];

	switch(TYPE(obj)) {
		case STR_T:
			obj_incref(obj);
			return obj;
		case CHAR_T:
			snprintf(buffer, BUFSIZE, "%c", obj_as_char(obj));
			return obj_create(STR_T, buffer);
		case INT_T:
			snprintf(buffer, BUFSIZE, "%ld", obj_as_int(obj));
			return obj_create(STR_T, buffer);
		case FLOAT_T:
			snprintf(buffer, BUFSIZE, "%.16lG", obj_as_float(obj));
			return obj_create(STR_T, buffer);
		case NONE_T:
			return obj_create(STR_T, "None");
		case POSITION_T:
			return obj_create(STR_T, "");
		default:
			return obj_create(STR_T, "");
	}
}


#ifdef DEBUG
/* Add object 'item' to the end of the object queue
 */
static void _enqueue(Object *item)
{
	if (head == NULL) {
		head = item;
		item->prevobj = NULL;
	} else {
		item->prevobj = tail;
		tail->nextobj = item;
	}
	tail = item;
	item->nextobj = NULL;
}
#endif


#ifdef DEBUG
/* Remove object 'item' from the object queue
 */
static void _dequeue(Object *item)
{
	if (item->nextobj == NULL) {  /* last element */
		if (item->prevobj == NULL) {  /* also first element */
			head = tail = NULL;  /* so empty the list */
		} else {  /* not also the first element */
			tail = item->prevobj;
			tail->nextobj = NULL;
		}
	} else {  /* not the last element */
		if (item->prevobj == NULL){  /* but the first element */
			head = item->nextobj;
			head->prevobj = NULL;
		} else {  /* somewhere in the middle of the list */
			item->prevobj->nextobj = item->nextobj;
			item->nextobj->prevobj = item->prevobj;
		}
	}
}
#endif


#ifdef DEBUG
/* Print all objects to a semi-colon separated file.
 *
 * Note: redirects stdout to a file. This cannot be undone in a
 *       cross-platform way, so only use when exiting the interpreter.
 */
void dump_object(void)
{
	FILE *fp;

	if ((fp = freopen("object.dsv", "w", stdout)) != NULL) {
		printf("object;refcount;type;value\n");
		for (Object *obj = head; obj; obj = obj->nextobj) {
			printf("%p;%d;%s;", (void *)obj, obj->refcount,TYPENAME(obj));
			obj_print(obj);
			printf("\n");
		}
		fclose(fp);
	}
}
#endif
