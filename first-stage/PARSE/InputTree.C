/*
 * Licensed under the Apache License, Version 2.0 (the "License"); you may
 * not use this file except in compliance with the License.  You may obtain
 * a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations
 * under the License.
 */

#include <assert.h>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include "InputTree.h"
#include "headFinder.h"
#include "utils.h"
#include "Term.h"

  
int              InputTree::pageWidth = 75; //used for prettyPrinting
ECString         InputTree::tempword[MAXSENTLEN];
int              InputTree::tempwordnum = 0;

// reset internal state before reporting an error
#define TREEREADINGERROR(message) \
    { InputTree::init(); error(__FILE__, __LINE__, message); }

InputTree::
InputTree(InputTree* it) :
  start_(it->start_), finish_(it->finish_), word_(it->word_),
    term_(it->term_), ntInfo_(it->ntInfo_), parent_(it->parent_)
{
  InputTreesIter subti = it->subTrees().begin();
  for( ; subti != it->subTrees().end() ; subti++)
    subTrees_.push_back(*subti);
}
     
void
InputTree::
init()
{
  for(int i = 0 ; i < MAXSENTLEN ; i++)
    {
      tempword[i] = "";
    }
  tempwordnum = 0;
}

InputTree::
~InputTree()
{
  InputTree  *subTree;
  InputTreesIter  subTreeIter = subTrees_.begin();
  for( ; subTreeIter != subTrees_.end() ; subTreeIter++ )
    {
      subTree = *subTreeIter;
      delete subTree;
    }
}

InputTree::
InputTree(istream& is)
{
  readParse(is);
}

istream&
operator >>( istream& is, InputTree& parse )
{
  if(parse.word() != "" || parse.term() != ""
     || parse.subTrees().size() > 0)
    TREEREADINGERROR("Reading into non-empty parse.");
  parse.readParse(is);
  return is;
}


void
InputTree::
readParse(istream& is)
{
  int pos = 0;
  start_ = pos;
  finish_ = pos;
  num_ = -1;

  ECString temp = readNext(is);
  if(!is) return;
  if(temp != "(")
    {
      TREEREADINGERROR("Saw '" + temp + "' instead of open paren here.");
    }
  /* get annotated symbols like NP-OBJ.  term_ = NP ntInfo_ = OBJ */
  temp = readNext(is);
  term_ = "S1";
  if(temp != "(")
    {
      if(temp == "S1" || temp == "TOP" || temp == "ROOT")
	{
	  temp = readNext(is);
	}
      else TREEREADINGERROR("Did not see legal topmost type");
    }
  if(temp == ")") return;
  if(temp != "(")
    {
      TREEREADINGERROR("Saw '" + temp + "' instead of second open paren here.");
    }

  for (;;)
    {
      InputTree* nextTree = newParse(is, pos, this);
      subTrees_.push_back(nextTree);
      finish_ = pos;

      headTree_ = nextTree->headTree_;
      temp = readNext(is);
      if (temp==")") break;
      if (temp!="(")
	{
      stringstream message("Saw '");
      message << *this << "' instead of open or closed paren here.";
      TREEREADINGERROR(message.str());
	}
    }
}

InputTree*
InputTree::
newParse(istream& is, int& strt, InputTree* par)
{
  int strt1 = strt;
  ECString wrd;
  ECString trm;
  ECString ntInf;
  InputTrees subTrs;
  int num = -1;

  parseTerm(is, trm, ntInf, num);
  for (;;) {
      ECString temp = readNext(is);
      if (temp == "(") {
          InputTree* nextTree = newParse(is, strt, NULL);
          if (nextTree) {
              subTrs.push_back(nextTree);
          }
      }
      else if (temp == ")") {
          break;
      }
      else if (temp == "") {
          TREEREADINGERROR("Unexpected end of string.");
      }
      else {
          if (trm != "-NONE-") {
              wrd = temp;
              strt++;
          }
      }
  }

  /* the Chinese treebank has a single POS for all punctuation,
     which is pretty bad for the parser, so make each punc its own POS */
  /* fixes bugs in Chinese Treebank */
  if(Term::Language == "Ch")
    {
      if (trm == "PU" && Term::get(wrd)) {
        trm = wrd;
      }
      const Term* ctrm = Term::get(trm);
      if(!ctrm)
	{
      TREEREADINGERROR("No such term: " + trm);
	}
      if(wrd!="" && !(ctrm->terminal_p()))
	{
	  cout<<trm<<wrd<<" changed to NN"<<endl;
	  trm="NN";
	}
      if(wrd=="" && ctrm->terminal_p() )
	{
	  cout<<trm<<" changed to NP"<<endl;
	  trm="NP";
	}
    }

  InputTree* ans = new InputTree(strt1, strt, wrd, trm, ntInf, subTrs,
				 par, NULL);
  ans->num() = num;
  InputTreesIter iti = subTrs.begin();
  for(; iti != subTrs.end() ; iti++)
    {
      InputTree* st = *iti;
      st->parentSet() = ans;
    }
  
  if(wrd == "" && subTrs.size() == 0) return NULL;
  if(wrd != "")
    {
      ans->headTree() = ans;
    }
  else
    {
      int hpos = headPosFromTree(ans);
      ans->headTree() = ithInputTree(hpos, subTrs)->headTree();
    }
  //cerr << "ANS " << ans->start() << " " << *ans << endl;
  return ans;
}

ECString&
InputTree::
readNext( istream& is ) 
{
  // if we already have a word, use it, and increment pointer to next word;
  //cerr << "RN1 " << tempwordnum << " " << tempword[tempwordnum] << endl;
  if( tempword[tempwordnum] != "" )
    {
      return tempword[tempwordnum++];
    }
  //cerr << "RN2" << endl;
  // else zero out point and stuff in 
  unsigned int tempnum;
  for(tempnum = 0 ; tempword[tempnum] != "" ; tempnum++ )
    tempword[tempnum] = "";
  tempwordnum = 0;
  // then go though next input, separating "()[]";
  int    wordnum  = 0 ;
  ECString  temp;
  is >> temp;
  //cerr << "In readnext " << temp << endl;
  for( tempnum = 0 ; tempnum < temp.length() ; tempnum++ )
    {
      char tempC = temp[tempnum];
      if(tempC == '(' || tempC == ')' ||
	 tempC == '[' || tempC == ']' )
	{
	  if( tempword[wordnum] != "" )
	    wordnum++;
	  tempword[wordnum++] += tempC;
	}
      else tempword[wordnum] += temp[tempnum];
    }
  return tempword[tempwordnum++];
}

/* if we see NP-OBJ make NP a, and -OBJ b */
void
InputTree::
parseTerm(istream& is, ECString& a, ECString& b, int& num)
{
  ECString temp = readNext(is);
  if(temp == "(" || temp == ")") TREEREADINGERROR("Saw paren rather than term");
  unsigned int len = temp.length();
  size_t pos;
  pos = temp.find("^");
  if(pos < len && pos > 0)
    {
      ECString na(temp, 0, pos);
      ECString nb(temp, pos+1, len-pos-1);
      a = na;
      // cerr <<"NB " << na << " " << nb << endl;
      num = atoi(nb.c_str());
    }
  else a = temp;
  pos = a.find("-");
  /* things like -RCB- will have a - at position 0 */
  if(pos < len && pos > 0)
    {
      ECString na(a, 0, pos);
      ECString nb(a, pos, len-pos);
      a = na;
      len = pos;
      b = nb;
    }
  else
    {
      b = "";
    }
  pos = a.find("=");
  if(pos < len && pos > 0)
    {
      ECString na(temp, 0, pos);
      ECString nb(temp, pos, len-pos);
      a = na;
      len = pos;
      b += nb;
    }
  pos = a.find("|");
  if(pos < len && pos > 0)
    {
      ECString na(temp, 0, pos);
      ECString nb(temp, pos, len-pos);
      a = na;
      b += nb;
    }
}	   
	   
ostream&
operator <<( ostream& os, const InputTree& parse )
{
  parse.prettyPrint( os, 0, false );
  return os;
}

void 
InputTree::
printproper( ostream& os ) const
{
  if( word_.length() != 0 )
    {
      os << "(" << term_ << " " << word_ << ")";
    }
  else
    {
      os << "(";
      os <<  term_ << ntInfo_;
      ConstInputTreesIter  subTreeIter= subTrees_.begin();
      InputTree  *subTree;
      for( ; subTreeIter != subTrees_.end() ; subTreeIter++ )
	{
	  subTree = *subTreeIter;
	  os << " ";
	  subTree->printproper( os );
	}
      os << ")";
    }
}

void 
InputTree::
prettyPrint(ostream& os, int start, bool startingLine) const              
{
  if(start >= pageWidth) //if we indent to much, just give up and print it.
    {
      printproper(os);
      return;
    }
  if(startingLine)
    {
      os << "\n";
      int numtabs = start/8;
      int numspace = start%8;
      int i;
      for( i = 0 ; i < numtabs ; i++ ) os << "\t"; //indent;
      for( i = 0 ; i < numspace ; i++ ) os << " "; //indent;
    }
  /* if there is enough space to print the rest of the tree, do so */
  if(spaceNeeded() <= pageWidth-start || word_ != "")
    {
      printproper(os);
    }
  else
    {
      os << "(";
      os << term_ << ntInfo_;
      os << " ";
      /* we need 2 extra spaces, for "(", " "  */
      int newStart = start + 2 + term_.length() + ntInfo_.length();
      //move start to right of space after term_ for next thing to print
      start++; //but for rest just move one space in.
      ConstInputTreesIter  subTreeIter = subTrees_.begin();
      InputTree  *subTree;
      int whichSubTree = 0;
      for( ; subTreeIter != subTrees_.end() ; subTreeIter++ )
	{
	  subTree = *subTreeIter;
	  if(whichSubTree++ == 0)
	    {
	      subTree->prettyPrint(os, newStart, false);
	    }
	  else
	    {
	      subTree->prettyPrint(os, start, true);
	    }
	}
      os << ")";
    }
}

/* estimates how much space we need to print the rest of the currently
   print tree */
int
InputTree::
spaceNeeded() const
{
  int needed = 1; // 1 for blank;    
  int wordLen = word_.length();
  needed += wordLen;
  needed += 3; //3 for () and " ";
  needed += term_.length();
  needed += ntInfo_.length();
  if(word_ != "") return needed;
  ConstInputTreesIter  subTreeIter = subTrees_.begin();
  InputTree  *subTree;
  for( ; subTreeIter != subTrees_.end() ; subTreeIter++ )
    {
      subTree = *subTreeIter;
      needed += subTree->spaceNeeded();
    }
  return needed;
}

void
InputTree::
make(list<ECString>& words)
{
  if(word_ != "")
    {
      words.push_back(word_);
    }
  else
    {
      ConstInputTreesIter subTreeIter = subTrees().begin();
      InputTree  *subTree;
      for(; subTreeIter != subTrees().end() ; subTreeIter++)
	{
	  subTree = *subTreeIter;
	  subTree->make(words);
	}
    }
}

void
InputTree::
makePosList(vector<ECString>& words)
{
  if(word_ != "")
    {
      words.push_back(term_);
    }
  else
    {
      ConstInputTreesIter subTreeIter = subTrees().begin();
      InputTree  *subTree;
      for(; subTreeIter != subTrees().end() ; subTreeIter++)
	{
	  subTree = *subTreeIter;
	  subTree->makePosList(words);
	}
    }
}



InputTree*
ithInputTree(int i, const list<InputTree*> l)
{
  list<InputTree*>::const_iterator li = l.begin();
  for(int j = 0 ; j < i ; j++)
    {
      assert(li != l.end());
      li++;
    }
  return *li;
}
