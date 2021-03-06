#ifndef GACTIONS_H_
#define GACTIONS_H_

#include "lexer.hpp"

// Color constants
#define NABU_RESET_COLOR        "\033[0m"
#define NABU_BOLD_COLOR         "\033[1m"
#define NABU_ERROR_COLOR        "\033[91;1m"
#define NABU_WARNING_COLOR      "\033[93;1m"

////////////////////////////////////
// Grammar aliases and structures //
////////////////////////////////////

// Term of an expression
using term = option <identifier, str>;

// Rule action
//      the body ca be on the next line
using rule_action = alias <
                action,
                fargs,
                option <newline, void>,
                fbody
        >;

// Assignment statements
using assignment = alias <
		identifier,
		walrus,
      		repeat <term, -1, 1>,
		option <rule_action, void>
	>;

// All valid statements
using statement = alias <
		option <macro, assignment, void>,
		newline
	>;

//////////////////
// Nabu context //
//////////////////

struct Glob {
	// Argument parser
	ArgParser ap = ArgParser("nabu");

	// Modes for parsing
	enum Mode : int {
		LEXER,
		RULES,
		PARSER_RD
	};

	// Current mode
	int mode = PARSER_RD;

	// Lexers
	struct Lexer {
		std::string name;
		std::string regex;

		// Optional converter
		std::string converter;
	};

	std::vector <Lexer> lexers;

	// Parsers
	struct Parser {
		std::string name;
		std::string code;

		// Optional grammar action
                std::vector <std::string> args;
		std::string action;
	};

	std::vector <Parser> parsers;

	// Error at line and column
	// TODO: add file as well (as variable)
	void error(int line, int column, std::string str) {
		fprintf(stderr, "%s%d:%d: %serror:%s %s\n",
			NABU_BOLD_COLOR, line, column,
			NABU_ERROR_COLOR,
			NABU_RESET_COLOR, str.c_str());
		exit(1);
	}

	// TODO: error which shows the text and squiggles
};

extern Glob glob;

// Macro handler
template <>
struct grammar_action <macro> {
	static constexpr bool available = true;

	// Macro related variables
	static struct {
		std::string project;
		std::string entry;
	} state;

	// Macro handlers
	static void project(parser::Queue &q) {
		parser::lexicon lptr = q.front();
		if (lptr->id == token <identifier> ::id) {
			state.project = get <std::string> (lptr);
			q.pop_front();
		}
	}

	static void entry(parser::Queue &q) {
		if (!parser::expect <identifier, std::string> (q, state.entry)) {
			std::cout << "Expected identifier after @entry!\n";
		}
	}

	static void rules(parser::Queue &q) {
		std::cout << "Entering rules section\n";
		glob.mode = Glob::RULES;
	}

	static void lexer(parser::Queue &q) {
		std::cout << "Entering lexer/regex section\n";
		glob.mode = Glob::LEXER;
	}

	static void parser_rd(parser::Queue &q) {
		std::cout << "Entering parser RD section\n";
		glob.mode = Glob::PARSER_RD;
	}

	// Grammar action
	static void action(parser::lexicon lptr, parser::Queue &q) {
		static std::unordered_map <
				std::string,
				void (*)(parser::Queue &)
			> handlers {
			{"@project", &project},
			{"@entry", &entry},
			{"@rules", &rules},
			{"@lexer", &lexer},
			{"@parser-rd", &parser_rd}
		};

		// std::cout << "MACRO: " << lptr->str() << std::endl;

		std::string macro = get <std::string> (lptr);
		if (handlers.find(macro) == handlers.end()) {
			// std::cerr << "Unknown macro " << macro << std::endl;
			// TODO: each lexicon should have an associated line
			// number so that error handling can be made easier
			// (and column index)
			glob.error(
				lptr->line, lptr->col,
				"Unknown macro " + macro
			);
		}

		// Handle the macro
		handlers[macro](q);

		// Expect a newline
		if (!parser::expect <newline> (q)) {
			std::cerr << "Expected newline after macro!\n";
			exit(1);
		}
	}
};

// Overload grammar for assignments
template <>
struct grammar_action <assignment> {
	static constexpr bool available = true;

	// Create the assignment from the list of terms
	static std::string mk_term(const parser::lexicon &lptr) {
		if (lptr->id == token <identifier> ::id)
			return get <std::string> (lptr);
		return "?";
	}

        // Extract arguments from a string of the form (a, b, c, ...)
        static std::vector <std::string> extract_args(const std::string sig)
        {
                std::vector <std::string> args;

                std::string arg;
                for (char c : sig) {
                        if (c == ',' || c == ')') {
                                args.push_back(arg);
                                std::cout << "Pushed " << arg << std::endl;
                                arg.clear();
                        } else if (c != '(') {
                                arg += c;
                        }
                }

                return args;
        }

	// Push an assignment to the glob
	static void push_parser(const std::string &ident,
			const std::vector <parser::lexicon> &terms,
                        const std::vector <parser::lexicon> &converter) {
		std::string code = "nabu::parser::rd::alias <";

		// Create the code
		bool first = true;
		for (const auto &term : terms) {
			if (first)
				first = false;
			else
				code += ", ";

			code += mk_term(term);
		}

		// Closde the code
		code += ">";

                // Extract arguments and body if any
                std::vector <std::string> args;
                std::string body;

                if (converter.size() > 0) {
                        parser::lexicon largs = converter[1];
                        parser::lexicon lbody = converter[3];

                        args = extract_args(get <std::string> (largs));
                        body = get <std::string> (lbody);
                }

		glob.parsers.push_back(
			{ident, code, args, body}
		);
	}

	// Create a lexer with regex
	static void push_lexer(const std::string &ident,
			const std::vector <parser::lexicon> &terms,
                        const std::vector <parser::lexicon> &action) {
		std::string regex;

		// TODO: generalize later (compound regexes)
		regex = get <std::string> (terms[0]);
		glob.lexers.push_back(
			{ident, regex}
		);
	}

	// Grammar action
	static void action(parser::lexicon lptr, parser::Queue &q) {
		std::vector <lexicon> v = tovec(lptr);

		std::string name = get <std::string> (v[0]);
		std::vector <lexicon> terms = tovec(v[2]);
                std::vector <lexicon> converter;

                // Check if converter or action is present
                if (v[3]->id == token <std::vector <lexicon>> ::id)
                    converter = tovec(v[3]);

		// TODO: array of lexicons
		switch (glob.mode) {
		case Glob::LEXER:
			push_lexer(name, terms, converter);
			break;
		case Glob::PARSER_RD:
			push_parser(name, terms, converter);
			break;
		default:
			std::cerr << "Unexpected assignment in mode "
				<< glob.mode << std::endl;
			exit(1);
		}
	}
};

#endif
