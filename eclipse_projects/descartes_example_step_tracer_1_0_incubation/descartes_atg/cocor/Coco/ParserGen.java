/*-------------------------------------------------------------------------
ParserGen.java -- Generation of the Recursive Descent Parser
Compiler Generator Coco/R,
Copyright (c) 1990, 2004 Hanspeter Moessenboeck, University of Linz
extended by M. Loeberbauer & A. Woess, Univ. of Linz
ported from C# to Java by Wolfgang Ahorner
with improvements by Pat Terry, Rhodes University

This program is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the
Free Software Foundation; either version 2, or (at your option) any
later version.

This program is distributed in the hope that it will be useful, but
WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
for more details.

You should have received a copy of the GNU General Public License along
with this program; if not, write to the Free Software Foundation, Inc.,
59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.

As an exception, it is allowed to write an extension of Coco/R that is
used as a plugin in non-free software.

If not otherwise stated, any source code generated by Coco/R (other than
Coco/R itself) does not fall under the GNU General Public License.
------------------------------------------------------------------------*/

package Coco;

import java.io.File;
import java.io.FileNotFoundException;
import java.io.Reader;             /* pdt */
import java.io.BufferedReader;     /* pdt */
import java.io.FileReader;         /* pdt */
import java.io.StringWriter;
import java.io.PrintWriter;        /* pdt */
import java.io.BufferedWriter;     /* pdt */
import java.io.FileWriter;         /* pdt */
import java.util.ArrayList;
import java.util.BitSet;

public class ParserGen {

  static final int maxTerm = 3;   // sets of size < maxTerm are enumerated
  static final char CR  = '\r';
  static final char LF  = '\n';
  static final int EOF = -1;
  static final String ls = System.getProperty("line.separator");

  static final int tErr = 0;      // error codes
  static final int altErr = 1;
  static final int syncErr = 2;

  public Position usingPos; // "using" definitions from the attributed grammar

  int errorNr;       // highest parser error number
  Symbol curSy;      // symbol whose production is currently generated
  Reader fram;       // parser frame file
  PrintWriter gen;   // generated parser source file
  StringWriter err;  // generated parser error messages
  String srcName;    // name of attributed grammar file
  String srcDir;     // directory of attributed grammar file
  ArrayList symSet = new ArrayList();

  Tab tab;           // other Coco objects
  Trace trace;
  Errors errors;
  Buffer buffer;

  private int framRead() {
    try {
      return fram.read();
    } catch (java.io.IOException e) {
      errors.Exception("-- error reading Parser.frame");
      return EOF;
    }
  }

  // --------------------------------------------------------------------------

  void Indent (int n) {
    for (int i = 1; i <= n; i++) gen.print('\t');
  }

  // AW: use a switch if more than 5 alternatives and none starts with a resolver
  boolean UseSwitch (Node p) {
    if (p.typ != Node.alt) return false;
    int nAlts = 0;
    while (p != null) {
      ++nAlts;
      // must not optimize with switch-statement, if alt uses a resolver expression
      if (p.sub.typ == Node.rslv) return false;
      p = p.down;
    }
    return nAlts > 5;
  }

  void CopyFramePart (String stop) {
    char startCh = stop.charAt(0);
    int endOfStopString = stop.length()-1;
    int ch = framRead();
    while (ch != EOF)
      if (ch == startCh) {
        int i = 0;
        do {
          if (i == endOfStopString) return; // stop[0..i] found
          ch = framRead(); i++;
        } while (ch == stop.charAt(i));
        // stop[0..i-1] found; continue with last read character
        gen.print(stop.substring(0, i));
      } else {
        gen.print((char)ch); ch = framRead();
      }
    errors.Exception(" -- incomplete or corrupt parser frame file");
  }

  void CopySourcePart (Position pos, int indent) {
    // Copy text described by pos from atg to gen
    int ch, nChars, i;
    if (pos != null) {
      buffer.setPos(pos.beg); ch = buffer.Read(); nChars = pos.len - 1;
      Indent(indent);
      boolean done = false;
      while (nChars >= 0) {
        while (ch == CR || ch == LF) {  // eol is either CR or CRLF or LF
          gen.println(); Indent(indent);
          if (ch == CR) { ch = buffer.Read(); nChars--; }  // skip CR
          if (ch == LF) { ch = buffer.Read(); nChars--; }  // skip LF
          for (i = 1; i <= pos.col && ch <= ' '; i++) {
            // skip blanks at beginning of line
            ch = buffer.Read(); nChars--;
          }
          if (i <= pos.col) pos.col = i - 1; // heading TABs => not enough blanks
          done = (nChars < 0);
        }
        if (done) break;
        gen.print((char)ch);
        ch = buffer.Read(); nChars--;
      }
      if (indent > 0) gen.println();
    }
  }

  void GenErrorMsg (int errTyp, Symbol sym) {
    errorNr++;
    err.write(ls + "\t\t\tcase " + errorNr + ": s = \"");
    switch (errTyp) {
      case tErr:
        if (sym.name.charAt(0) == '"') err.write(tab.Escape(sym.name) + " expected");
        else err.write(sym.name + " expected");
        break;
      case altErr: err.write("invalid " + sym.name); break;
      case syncErr: err.write("this symbol not expected in " + sym.name); break;
    }
    err.write("\"; break;");
  }

  int NewCondSet (BitSet s) {
    for (int i = 1; i < symSet.size(); i++) // skip symSet[0] (reserved for union of SYNC sets)
      if (Sets.Equals(s, (BitSet)symSet.get(i))) return i;
    symSet.add(s.clone());
    return symSet.size() - 1;
  }

  void GenCond (BitSet s, Node p) {
    if (p.typ == Node.rslv) CopySourcePart(p.pos, 0);
    else {
      int n = Sets.Elements(s);
      if (n == 0) gen.print("false"); // should never happen
      else if (n <= maxTerm) {
        for (int i = 0; i < tab.terminals.size(); i++) {
          Symbol sym = (Symbol)tab.terminals.get(i);
          if (s.get(sym.n)) {
            gen.print("la.kind == " + sym.n);
            --n;
            if (n > 0) gen.print(" || ");
          }
        }
      } else
        gen.print("StartOf(" + NewCondSet(s) + ")");
      /*
      if (p.typ == Node.alt) {
        // for { ... | IF ... | ... } or [ ... | IF ... | ... ]
        // check resolvers in addition to terminal start symbols of alternatives
        Node q = p;
        while (q != null) {
          if (q.sub.typ == Node.rslv) {
            gen.print(" || ");
            CopySourcePart(q.sub.pos, 0);
          }
          q = q.down;
        }
      }
      */
    }
  }

  void PutCaseLabels (BitSet s) {
    for (int i = 0; i < tab.terminals.size(); i++) {
      Symbol sym = (Symbol)tab.terminals.get(i);
      if (s.get(sym.n)) gen.print("case " + sym.n + ": ");
    }
  }

  void GenCode (Node p, int indent, BitSet isChecked) {
    Node p2;
    BitSet s1, s2;
    while (p != null) {
      switch (p.typ) {
        case Node.nt: {
          Indent(indent);
          if (p.retVar != null) gen.print(p.retVar + " = ");
          gen.print(p.sym.name + "(");
          CopySourcePart(p.pos, 0);
          gen.println(");");
          break;
        }
        case Node.t: {
          Indent(indent);
          if (isChecked.get(p.sym.n)) gen.println("Get();");
          else gen.println("Expect(" + p.sym.n + ");");
          break;
        }
        case Node.wt: {
          Indent(indent);
          s1 = tab.Expected(p.next, curSy);
          s1.or(tab.allSyncSets);
          gen.println("ExpectWeak(" + p.sym.n + ", " + NewCondSet(s1) + ");");
          break;
        }
        case Node.any: {
          Indent(indent);
          gen.println("Get();");
          break;
        }
        case Node.eps: break;  // nothing
        case Node.rslv: break; // nothing
        case Node.sem: {
          CopySourcePart(p.pos, indent);
          break;
        }
        case Node.sync: {
          Indent(indent);
          GenErrorMsg(syncErr, curSy);
          s1 = (BitSet)p.set.clone();
          gen.print("while (!("); GenCond(s1,p); gen.print(")) {");
          gen.print("SynErr(" + errorNr + "); Get();"); gen.println("}");
          break;
        }
        case Node.alt: {
          s1 = tab.First(p);
          boolean equal = Sets.Equals(s1, isChecked);
          boolean useSwitch = UseSwitch(p);
          if (useSwitch) { Indent(indent); gen.println("switch (la.kind) {"); }
          p2 = p;
          while (p2 != null) {
            s1 = tab.Expected(p2.sub, curSy);
            Indent(indent);
            if (useSwitch) {
              PutCaseLabels(s1); gen.println("{");
            } else if (p2 == p) {
              gen.print("if ("); GenCond(s1, p2.sub); gen.println(") {");
            } else if (p2.down == null && equal) { gen.println("} else {");
            } else {
              gen.print("} else if (");  GenCond(s1, p2.sub); gen.println(") {");
            }
            s1.or(isChecked);
            //if (p2.sub.typ != Node.rslv) GenCode(p2.sub, indent + 1, s1);
            //else GenCode(p2.sub.next, indent + 1, s1);
            GenCode(p2.sub, indent + 1, s1);
            if (useSwitch) {
              Indent(indent); gen.println("\tbreak;");
              Indent(indent); gen.println("}");
            }
            p2 = p2.down;
          }
          Indent(indent);
          if (equal) {
            gen.println("}");
          } else {
            GenErrorMsg(altErr, curSy);
            if (useSwitch) {
              gen.println("default: SynErr(" + errorNr + "); break;");
              Indent(indent); gen.println("}");
            } else {
              gen.print("} "); gen.println("else SynErr(" + errorNr + ");");
            }
          }
          break;
        }
        case Node.iter: {
          Indent(indent);
          p2 = p.sub;
          gen.print("while (");
          if (p2.typ == Node.wt) {
            s1 = tab.Expected(p2.next, curSy);
            s2 = tab.Expected(p.next, curSy);
            gen.print("WeakSeparator(" + p2.sym.n + "," + NewCondSet(s1) + ","
              + NewCondSet(s2) + ") ");
            s1 = new BitSet(tab.terminals.size());  // for inner structure
            if (p2.up || p2.next == null) p2 = null; else p2 = p2.next;
          } else {
            s1 = tab.First(p2);
            GenCond(s1, p2);
          }
          gen.println(") {");
          GenCode(p2, indent + 1, s1);
          Indent(indent); gen.println("}");
          break;
        }
        case Node.opt:
          //if (p.sub.typ != Node.rslv) s1 = tab.First(p.sub);
          //else s1 = tab.First(p.sub.next);
            s1 = tab.First(p.sub);
            Indent(indent);
          gen.print("if ("); GenCond(s1, p.sub); gen.println(") {");
          //if (p.sub.typ != Node.rslv) GenCode(p.sub, indent + 1, s1);
          //else GenCode(p.sub.next, indent + 1, s1);
          GenCode(p.sub, indent + 1, s1);
          Indent(indent); gen.println("}");
          break;
      }
      if (p.typ != Node.eps && p.typ != Node.sem && p.typ != Node.sync)
        isChecked.set(0, isChecked.size(), false);  // = new BitArray(Symbol.terminals.Count);
      if (p.up) break;
      p = p.next;
    }
  }

  void GenTokens() {
    //foreach (Symbol sym in Symbol.terminals) {
    for (int i = 0; i < tab.terminals.size(); i++) {
      Symbol sym = (Symbol)tab.terminals.get(i);
      if (Character.isLetter(sym.name.charAt(0)))
        gen.println("\tstatic final int _" + sym.name + " = " + sym.n + ";");
    }
  }

  void GenCodePragmas() {
    //foreach (Symbol sym in Symbol.pragmas) {
    for (int i = 0; i < tab.pragmas.size(); i++) {
      Symbol sym = (Symbol)tab.pragmas.get(i);
      gen.println();
      gen.println("\t\t\tif (la.kind == " + sym.n + ") {");
      CopySourcePart(sym.semPos, 4);
      gen.print  ("\t\t\t}");
    }
  }

  void GenProductions() {
    for (int i = 0; i < tab.nonterminals.size(); i++) {
      Symbol sym = (Symbol)tab.nonterminals.get(i);
      curSy = sym;
      gen.print("\t");
      if (sym.retType == null) gen.print("void "); else gen.print(sym.retType + " ");
      gen.print(sym.name + "(");
      CopySourcePart(sym.attrPos, 0);
      gen.println(") {");
      if (sym.retVar != null) gen.println("\t\t" + sym.retType + " " + sym.retVar + ";");
      CopySourcePart(sym.semPos, 2);
      GenCode(sym.graph, 2, new BitSet(tab.terminals.size()));
      if (sym.retVar != null) gen.println("\t\treturn " + sym.retVar + ";");
      gen.println("\t}"); gen.println();
    }
  }

  void InitSets() {
    for (int i = 0; i < symSet.size(); i++) {
      BitSet s = (BitSet)symSet.get(i);
      gen.print("\t\t{");
      int j = 0;
      //foreach (Symbol sym in Symbol.terminals) {
      for (int k = 0; k < tab.terminals.size(); k++) {
        Symbol sym = (Symbol)tab.terminals.get(k);
        if (s.get(sym.n)) gen.print("T,"); else gen.print("x,");
        ++j;
        if (j%4 == 0) gen.print(" ");
      }
      if (i == symSet.size()-1) gen.println("x}"); else gen.println("x},");
    }
  }

  void OpenGen(boolean backUp) {
    try {
      String fn = tab.srcDir + "Parser.java";
      File f = new File(fn);
      if (backUp && f.exists()) {
        File old = new File(fn + ".old");
        old.delete(); f.renameTo(old);
      }
      gen = new PrintWriter(new BufferedWriter(new FileWriter(fn, false))); /* pdt */
    } catch (Exception e) {
      errors.Exception("-- Cannot generate parser file");
    }
  }

  public void WriteParser () {
		int oldPos = buffer.getPos();  // Buffer.pos is modified by CopySourcePart
    symSet.add(tab.allSyncSets);
    String fr = tab.srcDir + "Parser.frame";
    if (!new File(fr).exists()) {
      if (tab.frameDir != null) fr = tab.frameDir.trim() + System.getProperty("file.separator") + "Parser.frame";
      if (!new File(fr).exists()) errors.Exception("-- Cannot find Parser.frame");
    }
    try {
      fram = new BufferedReader(new FileReader(fr)); /* pdt */
    } catch (FileNotFoundException e) {
      errors.Exception("-- Cannot open Parser.frame.");
    }
    OpenGen(true);
    err = new StringWriter();
    //foreach (Symbol sym in Symbol.terminals)
    for (int i = 0; i < tab.terminals.size(); i++) {
      Symbol sym = (Symbol)tab.terminals.get(i);
      GenErrorMsg(tErr, sym);
    }

    CopyFramePart("-->begin");
    if (!tab.srcName.toLowerCase().endsWith("coco.atg")) {
      gen.close();
      OpenGen(false);
    }
    if (tab.nsName != null && tab.nsName.length() > 0) {
      gen.print("package ");
      gen.print(tab.nsName);
      gen.print(";");
    }
    if (usingPos != null) {
      gen.println(); gen.println();
      CopySourcePart(usingPos, 0);
    }
    CopyFramePart("-->constants");
    GenTokens();
    gen.println("\tstatic final int maxT = " + (tab.terminals.size()-1) + ";");
    CopyFramePart("-->declarations"); CopySourcePart(tab.semDeclPos, 0);
    CopyFramePart("-->pragmas"); GenCodePragmas();
    CopyFramePart("-->productions"); GenProductions();
    CopyFramePart("-->parseRoot"); gen.println("\t\t" + tab.gramSy.name + "();");
    CopyFramePart("-->initialization"); InitSets();
    CopyFramePart("-->errors"); gen.print(err.toString());
    CopyFramePart("$$$");
    gen.close();
    buffer.setPos(oldPos);
  }

  public void WriteStatistics () {
    trace.WriteLine();
    trace.WriteLine(tab.terminals.size() + " terminals");
    trace.WriteLine(tab.terminals.size() + tab.pragmas.size() +
                                   tab.nonterminals.size() + " symbols");
    trace.WriteLine(tab.nodes.size() + " nodes");
    trace.WriteLine(symSet.size() + " sets");
  }

  public ParserGen (Parser parser) {
		tab = parser.tab;
		errors = parser.errors;
		trace = parser.trace;
		buffer = parser.scanner.buffer;
    errorNr = -1;
    usingPos = null;
  }

}