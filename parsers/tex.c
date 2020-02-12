/*
 *	 Copyright (c) 2008, David Fishburn
 *	 Copyright (c) 2012, Jan Larres
 *
 *	 This source code is released for free distribution under the terms of the
 *	 GNU General Public License version 2 or (at your option) any later version.
 *
 *	 This module contains functions for generating tags for TeX language files.
 *
 *	 Tex language reference:
 *		 http://en.wikibooks.org/wiki/TeX#The_Structure_of_TeX
 */

/*
 *	 INCLUDE FILES
 */
#include "general.h"	/* must always come first */
#include <ctype.h>	/* to define isalpha () */
#ifdef DEBUG
#include <stdio.h>
#endif
#include <string.h>

#include "debug.h"
#include "entry.h"
#include "keyword.h"
#include "parse.h"
#include "read.h"
#include "routines.h"
#include "vstring.h"

/*
 *	 MACROS
 */
#define isType(token,t)		(bool) ((token)->type == (t))
#define isKeyword(token,k)	(bool) ((token)->keyword == (k))
#define isIdentChar(c) \
	(isalpha (c) || isdigit (c) || (c) >= 0x80 || (c) == '$' || \
		(c) == '_' || (c) == '#' || (c) == '-' || (c) == '.' || (c) == ':')

/*
 *	 DATA DECLARATIONS
 */

/*
 * Used to specify type of keyword.
 */
enum eKeywordId {
	KEYWORD_part,
	KEYWORD_chapter,
	KEYWORD_section,
	KEYWORD_subsection,
	KEYWORD_subsubsection,
	KEYWORD_paragraph,
	KEYWORD_subparagraph,
	KEYWORD_label,
	KEYWORD_include
};
typedef int keywordId; /* to allow KEYWORD_NONE */

enum eTokenType {
	/* 0..255 are the byte's value.  Some are named for convenience */
	TOKEN_OPEN_PAREN = '(',
	TOKEN_CLOSE_PAREN = ')',
	TOKEN_OPEN_CURLY = '{',
	TOKEN_CLOSE_CURLY = '}',
	TOKEN_OPEN_SQUARE = '[',
	TOKEN_CLOSE_SQUARE = ']',
	TOKEN_STAR = '*',
	/* above is special types */
	TOKEN_UNDEFINED = 256,
	TOKEN_KEYWORD,
	TOKEN_IDENTIFIER,
	TOKEN_STRING,
};
typedef int tokenType;

typedef struct sTokenInfo {
	tokenType		type;
	keywordId		keyword;
	vString *		string;
	vString *		scope;
	unsigned long 	lineNumber;
	MIOPos 			filePosition;
} tokenInfo;

/*
 *	DATA DEFINITIONS
 */

static langType Lang_tex;

static vString *lastPart;
static vString *lastChapter;
static vString *lastSection;
static vString *lastSubS;
static vString *lastSubSubS;

typedef enum {
	TEXTAG_PART,
	TEXTAG_CHAPTER,
	TEXTAG_SECTION,
	TEXTAG_SUBSECTION,
	TEXTAG_SUBSUBSECTION,
	TEXTAG_PARAGRAPH,
	TEXTAG_SUBPARAGRAPH,
	TEXTAG_LABEL,
	TEXTAG_INCLUDE,
	TEXTAG_COUNT
} texKind;

static kindDefinition TexKinds [] = {
	{ true,  'p', "part",			  "parts"			   },
	{ true,  'c', "chapter",		  "chapters"		   },
	{ true,  's', "section",		  "sections"		   },
	{ true,  'u', "subsection",		  "subsections"		   },
	{ true,  'b', "subsubsection",	  "subsubsections"	   },
	{ true,  'P', "paragraph",		  "paragraphs"		   },
	{ true,  'G', "subparagraph",	  "subparagraphs"	   },
	{ true,  'l', "label",			  "labels"			   },
	{ true,  'i', "include",	  	  "includes"		   }
};

static const keywordTable TexKeywordTable [] = {
	/* keyword			keyword ID */
	{ "part",			KEYWORD_part				},
	{ "chapter",		KEYWORD_chapter				},
	{ "section",		KEYWORD_section				},
	{ "subsection",		KEYWORD_subsection			},
	{ "subsubsection",	KEYWORD_subsubsection		},
	{ "paragraph",		KEYWORD_paragraph			},
	{ "subparagraph",	KEYWORD_subparagraph		},
	{ "label",			KEYWORD_label				},
	{ "include",		KEYWORD_include				}
};

/*
 *	 FUNCTION DEFINITIONS
 */

static tokenInfo *newToken (void)
{
	tokenInfo *const token = xMalloc (1, tokenInfo);

	token->type			= TOKEN_UNDEFINED;
	token->keyword		= KEYWORD_NONE;
	token->string		= vStringNew ();
	token->scope		= vStringNew ();
	token->lineNumber   = getInputLineNumber ();
	token->filePosition = getInputFilePosition ();

	return token;
}

static void deleteToken (tokenInfo *const token)
{
	vStringDelete (token->string);
	vStringDelete (token->scope);
	eFree (token);
}

static int getScopeInfo(texKind kind, vString *const parentName)
{
	int parentKind = KIND_GHOST_INDEX;
	int i;

	/*
	 * Put labels separately instead of under their scope.
	 * Is this The Right Thing To Do?
	 */
	if (kind >= TEXTAG_LABEL) {
		goto out;
	}

	/*
	 * This abuses the enum internals somewhat, but it should be ok in this
	 * case.
	 */
	/* TODO: This loop and conditions can be squashed. */
	for (i = kind - 1; i >= TEXTAG_PART; --i) {
		if (i == TEXTAG_SUBSECTION && vStringLength(lastSubS) > 0) {
			parentKind = i;
			break;
		} else if (i == TEXTAG_SECTION && vStringLength(lastSection) > 0) {
			parentKind = i;
			break;
		} else if (i == TEXTAG_CHAPTER && vStringLength(lastChapter) > 0) {
			parentKind = i;
			break;
		} else if (i == TEXTAG_PART && vStringLength(lastPart) > 0) {
			parentKind = i;
			break;
		}
	}

	/*
	 * Is '""' the best way to separate scopes? It has to be something that
	 * should ideally never occur in normal LaTeX text.
	 */
	for (i = TEXTAG_PART; i < (int)kind; ++i) {
		if (i == TEXTAG_PART && vStringLength(lastPart) > 0) {
			vStringCat(parentName, lastPart);
		} else if (i == TEXTAG_CHAPTER && vStringLength(lastChapter) > 0) {
			if (vStringLength(parentName) > 0) {
				vStringCatS(parentName, "\"\"");
			}
			vStringCat(parentName, lastChapter);
		} else if (i == TEXTAG_SECTION && vStringLength(lastSection) > 0) {
			if (vStringLength(parentName) > 0) {
				vStringCatS(parentName, "\"\"");
			}
			vStringCat(parentName, lastSection);
		} else if (i == TEXTAG_SUBSECTION && vStringLength(lastSubS) > 0) {
			if (vStringLength(parentName) > 0) {
				vStringCatS(parentName, "\"\"");
			}
			vStringCat(parentName, lastSubS);
		}
	}
 out:
	return parentKind;
}

/*
 *	 Tag generation functions
 */
static void makeTexTag (tokenInfo *const token, texKind kind)
{
	if (TexKinds [kind].enabled)
	{
		const char *const name = vStringValue (token->string);
		int parentKind = KIND_GHOST_INDEX;
		vString *parentName = vStringNew();
		tagEntryInfo e;
		initTagEntry (&e, name, kind);

		e.lineNumber   = token->lineNumber;
		e.filePosition = token->filePosition;

		parentKind = getScopeInfo(kind, parentName);
		if (parentKind != KIND_GHOST_INDEX) {
			e.extensionFields.scopeKindIndex = parentKind;
			e.extensionFields.scopeName = vStringValue(parentName);
		}

		makeTagEntry (&e);
		vStringDelete (parentName);
	}
}

/*
 *	 Parsing functions
 */

/*
 *	Read a C identifier beginning with "firstChar" and places it into
 *	"name".
 */
static void parseIdentifier (vString *const string, const int firstChar)
{
	int c = firstChar;
	Assert (isIdentChar (c));
	do
	{
		vStringPut (string, c);
		c = getcFromInputFile ();
	} while (c != EOF && isIdentChar (c));

	if (c != EOF)
		ungetcToInputFile (c);		/* unget non-identifier character */
}

static bool readTokenFull (tokenInfo *const token, const bool includeWhitespaces)
{
	int c;
	int whitespaces = -1;

	token->type			= TOKEN_UNDEFINED;
	token->keyword		= KEYWORD_NONE;
	vStringClear (token->string);

getNextChar:

	do
	{
		c = getcFromInputFile ();
		whitespaces++;
	}
	while (c == '\t'  ||  c == ' ' ||  c == '\n');

	token->lineNumber   = getInputLineNumber ();
	token->filePosition = getInputFilePosition ();

	if (includeWhitespaces && whitespaces > 0 && c != '%' && c != EOF)
	{
		ungetcToInputFile (c);
		c = ' ';
	}

	token->type = (unsigned char) c;
	switch (c)
	{
		case EOF: return false;

		case '\\':
				  /*
				   * All Tex tags start with a backslash.
				   * Check if the next character is an alpha character
				   * else it is not a potential tex tag.
				   */
				  c = getcFromInputFile ();
				  if (! isalpha (c))
					  ungetcToInputFile (c);
				  else
				  {
					  vStringPut (token->string, '\\');
					  parseIdentifier (token->string, c);
					  token->keyword = lookupKeyword (vStringValue (token->string) + 1, Lang_tex);
					  if (isKeyword (token, KEYWORD_NONE))
						  token->type = TOKEN_IDENTIFIER;
					  else
						  token->type = TOKEN_KEYWORD;
				  }
				  break;

		case '%':
				  skipToCharacterInInputFile ('\n'); /* % are single line comments */
				  goto getNextChar;
				  break;

		default:
				  if (isIdentChar (c))
				  {
					  parseIdentifier (token->string, c);
					  token->type = TOKEN_IDENTIFIER;
				  }
				  break;
	}
	return true;
}

static bool readToken (tokenInfo *const token)
{
	return readTokenFull (token, false);
}

static void copyToken (tokenInfo *const dest, tokenInfo *const src)
{
	dest->lineNumber = src->lineNumber;
	dest->filePosition = src->filePosition;
	dest->type = src->type;
	dest->keyword = src->keyword;
	vStringCopy (dest->string, src->string);
	vStringCopy (dest->scope, src->scope);
}

static void updateScopeInfo (texKind kind, vString *fullname)
{
	switch (kind)
	{
		case TEXTAG_PART:
			vStringCopy(lastPart, fullname);
			vStringClear(lastChapter);
			vStringClear(lastSection);
			vStringClear(lastSubS);
			vStringClear(lastSubSubS);
			break;
		case TEXTAG_CHAPTER:
			vStringCopy(lastChapter, fullname);
			vStringClear(lastSection);
			vStringClear(lastSubS);
			vStringClear(lastSubSubS);
			break;
		case TEXTAG_SECTION:
			vStringCopy(lastSection, fullname);
			vStringClear(lastSubS);
			vStringClear(lastSubSubS);
			break;
		case TEXTAG_SUBSECTION:
			vStringCopy(lastSubS, fullname);
			vStringClear(lastSubSubS);
			break;
		case TEXTAG_SUBSUBSECTION:
			vStringCopy(lastSubSubS, fullname);
			break;
		default:
			break;
	}
}

/*
 *	 Scanning functions
 */

static bool parseTag (tokenInfo *const token, texKind kind, bool enterSquare)
{
	tokenInfo *const name = newToken ();
	vString *	fullname;
	bool		useLongName = true;
	bool        eof = false;

	fullname = vStringNew ();

	/*
	 * Tex tags are of these formats:
	 *   \keyword{any number of words}
	 *   \keyword[short desc]{any number of words}
	 *   \keyword*[short desc]{any number of words}
	 *
	 * When a keyword is found, loop through all words within
	 * the curly braces for the tag name.
	 *
	 * If the keyword is label like \label, words in the square
	 * brackets should be skipped. This can be controlled
	 * with `enterSquare' parameter; true is for tagging, and
	 * false is for skipping.
	 */

	if (isType (token, TOKEN_KEYWORD))
	{
		copyToken (name, token);
		if (!readToken (token))
		{
			eof = true;
			goto out;
		}
	}

	if (isType (token, TOKEN_OPEN_SQUARE))
	{
		if (enterSquare)
			useLongName = false;

		if (!readToken (token))
		{
			eof = true;
			goto out;
		}
		while (! isType (token, TOKEN_CLOSE_SQUARE) )
		{
			if (enterSquare
				&& isType (token, TOKEN_IDENTIFIER))
			{
				if (vStringLength (fullname) > 0)
					vStringPut (fullname, ' ');
				vStringCat (fullname, token->string);
			}
			if (!readToken (token))
			{
				eof = true;
				goto out;
			}
		}
		if (enterSquare)
		{
			vStringCopy (name->string, fullname);
			makeTexTag (name, kind);
		}
		else if (!readToken (token))
		{
			eof = true;
			goto out;
		}
	}

	if (isType (token, TOKEN_STAR))
	{
		if (!readToken (token))
		{
			eof = true;
			goto out;
		}
	}

	if (isType (token, TOKEN_OPEN_CURLY))
	{
		int depth = 1;

		if (!readToken (token))
		{
			eof = true;
			goto out;
		}

		/* Handle the case the code like \section{} */
		if (isType (token, TOKEN_CLOSE_CURLY))
			goto out;

		while (depth > 0)
		{
			/* if (isType (token, TOKEN_IDENTIFIER) && useLongName) */
			if (useLongName)
			{
				if (isType (token, TOKEN_IDENTIFIER) || isType (token, TOKEN_KEYWORD))
					vStringCat (fullname, token->string);
				else
					vStringPut (fullname, token->type);
			}
			if (!readTokenFull (token, useLongName))
			{
				eof = true;
				goto out;
			}
			else if (isType (token, TOKEN_OPEN_CURLY))
				depth++;
			else if (isType (token, TOKEN_CLOSE_CURLY))
				depth--;
		}
		if (useLongName)
		{
			vStringStripTrailing (fullname);
			if (vStringLength (fullname) > 0)
			{
				vStringCopy (name->string, fullname);
				makeTexTag (name, kind);
			}
		}
	}

	/*
	 * save the name of the last section definitions for scope-resolution
	 * later
	 */
	updateScopeInfo (kind, fullname);

 out:
	deleteToken (name);
	vStringDelete (fullname);
	return eof;
}

static void parseTexFile (tokenInfo *const token)
{
	bool eof = false;

	do
	{
		if (!readToken (token))
			break;

		if (isType (token, TOKEN_KEYWORD))
		{
			switch (token->keyword)
			{
				case KEYWORD_part:
					eof = parseTag (token, TEXTAG_PART, true);
					break;
				case KEYWORD_chapter:
					eof = parseTag (token, TEXTAG_CHAPTER, true);
					break;
				case KEYWORD_section:
					eof = parseTag (token, TEXTAG_SECTION, true);
					break;
				case KEYWORD_subsection:
					eof = parseTag (token, TEXTAG_SUBSECTION, true);
					break;
				case KEYWORD_subsubsection:
					eof = parseTag (token, TEXTAG_SUBSUBSECTION, true);
					break;
				case KEYWORD_paragraph:
					eof = parseTag (token, TEXTAG_PARAGRAPH, true);
					break;
				case KEYWORD_subparagraph:
					eof = parseTag (token, TEXTAG_SUBPARAGRAPH, true);
					break;
				case KEYWORD_label:
					eof = parseTag (token, TEXTAG_LABEL, false);
					break;
				case KEYWORD_include:
					eof = parseTag (token, TEXTAG_INCLUDE, true);
					break;
				default:
					break;
			}
		}
		if (eof)
			break;
	} while (true);
}

static void initialize (const langType language)
{
	Assert (ARRAY_SIZE (TexKinds) == TEXTAG_COUNT);
	Lang_tex = language;

	lastPart    = vStringNew();
	lastChapter = vStringNew();
	lastSection = vStringNew();
	lastSubS    = vStringNew();
	lastSubSubS = vStringNew();
}

static void finalize (const langType language CTAGS_ATTR_UNUSED,
		      bool initialized)
{
	if (initialized)
	{
		vStringDelete(lastPart);
		lastPart = NULL;
		vStringDelete(lastChapter);
		lastChapter = NULL;
		vStringDelete(lastSection);
		lastSection = NULL;
		vStringDelete(lastSubS);
		lastSubS = NULL;
		vStringDelete(lastSubSubS);
		lastSubSubS = NULL;
	}
}

static void findTexTags (void)
{
	tokenInfo *const token = newToken ();

	parseTexFile (token);

	deleteToken (token);
}

/* Create parser definition structure */
extern parserDefinition* TexParser (void)
{
	static const char *const extensions [] = { "tex", NULL };
	parserDefinition *const def = parserNew ("Tex");
	def->extensions = extensions;
	/*
	 * New definitions for parsing instead of regex
	 */
	def->kindTable	= TexKinds;
	def->kindCount	= ARRAY_SIZE (TexKinds);
	def->parser		= findTexTags;
	def->initialize = initialize;
	def->finalize   = finalize;
	def->keywordTable =  TexKeywordTable;
	def->keywordCount = ARRAY_SIZE (TexKeywordTable);
	return def;
}
