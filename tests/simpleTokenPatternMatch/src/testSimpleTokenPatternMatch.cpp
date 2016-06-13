/*
 * Copyright (c) 2014 Patrick P. Frey
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */
#include "strus/base/stdint.h"
#include "strus/lib/stream.hpp"
#include "strus/lib/error.hpp"
#include "strus/errorBufferInterface.hpp"
#include "strus/tokenPatternMatchInterface.hpp"
#include "strus/tokenPatternMatchInstanceInterface.hpp"
#include "strus/tokenPatternMatchContextInterface.hpp"
#include "strus/stream/patternMatchToken.hpp"
#include <stdexcept>
#include <iostream>
#include <sstream>
#include <cstdlib>
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <vector>
#include <memory>
#include <limits>
#include <ctime>
#include <cmath>
#include <cstring>
#include <iomanip>

#define STRUS_LOWLEVEL_DEBUG
#define RANDINT(MIN,MAX) ((std::rand()%(MAX-MIN))+MIN)

strus::ErrorBufferInterface* g_errorBuffer = 0;

struct DocumentItem
{
	unsigned int pos;
	unsigned int termid;

	DocumentItem( unsigned int pos_, unsigned int termid_)
		:pos(pos_),termid(termid_){}
	DocumentItem( const DocumentItem& o)
		:pos(o.pos),termid(o.termid){}
};

struct Document
{
	std::string id;
	std::vector<DocumentItem> itemar;

	explicit Document( const std::string& id_)
		:id(id_){}
	Document( const Document& o)
		:id(o.id),itemar(o.itemar){}
};

enum TermType {Token, SentenceDelim};
static unsigned int termId( TermType tp, unsigned int no)
{
	return ((unsigned int)tp << 24) + no;
}
#define TOKEN(no)  (((unsigned int)Token << 24) + no)
#define DELIM      (((unsigned int)SentenceDelim << 24) + 0)

static Document createDocument( unsigned int no, unsigned int size)
{
	char docid[ 32];
	snprintf( docid, sizeof(docid), "doc_%u", no);
	Document rt( docid);
	unsigned int ii = 0, ie = size;
	for (; ii < ie; ++ii)
	{
		unsigned int tok = ii+1;
		rt.itemar.push_back( DocumentItem( ii+1, termId( Token, tok)));
		if ((ii+1) % 10 == 0)
		{
			rt.itemar.push_back( DocumentItem( ii+2, termId( SentenceDelim, 0)));
		}
	}
	rt.itemar.push_back( DocumentItem( ++ii, termId( Token, 1)));
	rt.itemar.push_back( DocumentItem( ++ii, termId( Token, 1)));
	return rt;
}

typedef strus::TokenPatternMatchInstanceInterface::JoinOperation JoinOperation;
struct Operation
{
	enum Type {None,Term,Expression};
	Type type;
	unsigned int termid;
	unsigned int variable;
	JoinOperation joinop;
	unsigned int range;
	unsigned int cardinality;
	unsigned int argc;
};

struct Pattern
{
	const char* name;
	Operation operations[32];
	unsigned int results[32];
};

static void createPattern( strus::TokenPatternMatchInstanceInterface* ptinst, const char* ptname, const Operation* oplist)
{
	std::size_t oi = 0;
	for (; oplist[oi].type != Operation::None; ++oi)
	{
		const Operation& op = oplist[oi];
		switch (op.type)
		{
			case Operation::None:
				break;
			case Operation::Term:
				ptinst->pushTerm( op.termid);
				break;
			case Operation::Expression:
				ptinst->pushExpression( op.joinop, op.argc, op.range, op.cardinality);
				break;
		}
		if (op.variable)
		{
			char variablename[ 32];
			snprintf( variablename, sizeof(variablename), "A%u", (unsigned int)op.variable);
			ptinst->attachVariable( variablename, 1.0f);
		}
	}
	ptinst->definePattern( ptname, ptname[0] != '_');
}

static void createPatterns( strus::TokenPatternMatchInstanceInterface* ptinst, const Pattern* patterns)
{
	unsigned int pi=0;
	for (; patterns[pi].name; ++pi)
	{
		createPattern( ptinst, patterns[pi].name, patterns[pi].operations);
	}
}

static std::vector<strus::stream::TokenPatternMatchResult>
	matchRules( strus::TokenPatternMatchInstanceInterface* ptinst, const Document& doc)
{
	std::vector<strus::stream::TokenPatternMatchResult> results;
	std::auto_ptr<strus::TokenPatternMatchContextInterface> mt( ptinst->createContext());
	std::vector<DocumentItem>::const_iterator di = doc.itemar.begin(), de = doc.itemar.end();
	unsigned int didx = 0;
	for (; di != de; ++di,++didx)
	{
		mt->putInput( strus::stream::PatternMatchToken( di->termid, di->pos, didx, 1));
		if (g_errorBuffer->hasError()) throw std::runtime_error("error matching rules");
	}
	results = mt->fetchResults();

#ifdef STRUS_LOWLEVEL_DEBUG
	std::vector<strus::stream::TokenPatternMatchResult>::const_iterator
		ri = results.begin(), re = results.end();
	for (; ri != re; ++ri)
	{
		std::cout << "match '" << ri->name() << " at " << ri->ordpos() << "':";
		std::vector<strus::stream::TokenPatternMatchResultItem>::const_iterator
			ei = ri->items().begin(), ee = ri->items().end();

		for (; ei != ee; ++ei)
		{
			std::cout << " " << ei->name() << " [" << ei->ordpos()
					<< ", " << ei->origpos() << ", " << ei->origsize() << "]";
		}
		std::cout << std::endl;
	}
	strus::stream::TokenPatternMatchStatistics stats = mt->getStatistics();
	std::cout << "nof matches " << results.size();
	std::vector<strus::stream::TokenPatternMatchStatistics::Item>::const_iterator
		li = stats.items().begin(), le = stats.items().end();
	for (; li != le; ++li)
	{
		std::cout << ", " << li->name() << " " << (int)(li->value()+0.5);
	}
	std::cout << std::endl;
#endif
	return results;
}

typedef strus::TokenPatternMatchInstanceInterface PT;
static const Pattern testPatterns[32] =
{
	{"seq[3]_1_2",
		{{Operation::Term,TOKEN(1),1},
		 {Operation::Term,TOKEN(2),2},
		 {Operation::Expression,0,0,PT::OpSequence,3,0,2}},
		{1,0}
	},
	{"seq[2]_1_2",
		{{Operation::Term,TOKEN(1),1},
		 {Operation::Term,TOKEN(2),2},
		 {Operation::Expression,0,0,PT::OpSequence,2,0,2}},
		{1,0}
	},
	{"seq[1]_1_2",
		{{Operation::Term,TOKEN(1),1},
		 {Operation::Term,TOKEN(2),2},
		 {Operation::Expression,0,0,PT::OpSequence,1,0,2}},
		{1,0}
	},
	{"seq[2]_1_3",
		{{Operation::Term,TOKEN(1),1},
		 {Operation::Term,TOKEN(3),2},
		 {Operation::Expression,0,0,PT::OpSequence,2,0,2}},
		{1,0}
	},
	{"seq[1]_1_3",
		{{Operation::Term,TOKEN(1),1},
		 {Operation::Term,TOKEN(3),2},
		 {Operation::Expression,0,0,PT::OpSequence,1,0,2}},
		{0}
	},
	{"seq[1]_1_1",
		{{Operation::Term,TOKEN(1),1},
		 {Operation::Term,TOKEN(1),2},
		 {Operation::Expression,0,0,PT::OpSequence,1,0,2}},
		{101,0}
	},
	{0,{Operation::None}}
};

int main( int argc, const char** argv)
{
	try
	{
		g_errorBuffer = strus::createErrorBuffer_standard( 0, 1);
		if (!g_errorBuffer)
		{
			std::cerr << "construction of error buffer failed" << std::endl;
			return -1;
		}
		else if (argc > 1)
		{
			std::cerr << "too many arguments" << std::endl;
			return 1;
		}
		unsigned int documentSize = 100;

		std::auto_ptr<strus::TokenPatternMatchInterface> pt( strus::createTokenPatternMatch_standard( g_errorBuffer));
		if (!pt.get()) throw std::runtime_error("failed to create pattern matcher");
		std::auto_ptr<strus::TokenPatternMatchInstanceInterface> ptinst( pt->createInstance());
		if (!ptinst.get()) throw std::runtime_error("failed to create pattern matcher instance");
		createPatterns( ptinst.get(), testPatterns);
		ptinst->compile( strus::stream::TokenPatternMatchOptions());

		if (g_errorBuffer->hasError())
		{
			throw std::runtime_error( "error creating automaton for evaluating rules");
		}
		Document doc = createDocument( 1, documentSize);
		std::cerr << "starting rule evaluation ..." << std::endl;

		// Evaluate results:
		std::vector<strus::stream::TokenPatternMatchResult> 
			results = matchRules( ptinst.get(), doc);

		// Verify results:
		std::vector<strus::stream::TokenPatternMatchResult>::const_iterator
			ri = results.begin(), re = results.end();

		typedef std::pair<std::string,unsigned int> Match;
		std::set<Match> matches;
		for (;ri != re; ++ri)
		{
			matches.insert( Match( ri->name(), ri->ordpos()));
		}
		unsigned int ti=0;
		for (; testPatterns[ti].name; ++ti)
		{
			unsigned int ei=0;
			for (; testPatterns[ti].results[ei]; ++ei)
			{
				std::set<Match>::iterator
					mi = matches.find( Match( testPatterns[ti].name, testPatterns[ti].results[ei]));
				if (mi == matches.end())
				{
					char numbuf[ 64];
					::snprintf( numbuf, sizeof(numbuf), "%u", testPatterns[ti].results[ei]);
					throw std::runtime_error( std::string("expected match not found '") + testPatterns[ti].name + "' at ordpos " + numbuf);
				}
				else
				{
					matches.erase( mi);
				}
			}
		}
		if (!matches.empty())
		{
			std::set<Match>::const_iterator mi = matches.begin(), me = matches.end();
			for (; mi != me; ++mi)
			{
				std::cerr << "unexpected match of '" << mi->first << "' at ordpos " << mi->second << std::endl;
			}
			throw std::runtime_error( "more matches found than expected");
		}
		if (g_errorBuffer->hasError())
		{
			throw std::runtime_error("error matching rule");
		}
		std::cerr << "OK" << std::endl;
		delete g_errorBuffer;
		return 0;
	}
	catch (const std::runtime_error& err)
	{
		if (g_errorBuffer->hasError())
		{
			std::cerr << "error processing pattern matching: "
					<< g_errorBuffer->fetchError() << " (" << err.what()
					<< ")" << std::endl;
		}
		else
		{
			std::cerr << "error processing pattern matching: "
					<< err.what() << std::endl;
		}
	}
	catch (const std::bad_alloc&)
	{
		std::cerr << "out of memory processing pattern matching" << std::endl;
	}
	delete g_errorBuffer;
	return -1;
}

