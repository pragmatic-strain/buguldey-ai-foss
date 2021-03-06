package org.opencyc.util;

import java.io.*;
import java.util.*;
import junit.framework.*;

/**
 * Provides a suite of JUnit test cases for the <tt>org.opencyc.constraintsolver</tt> package.<p>
 *
 * @version $Id: UnitTest.java,v 1.14 2002/10/23 14:44:59 stephenreed Exp $
 * @author Stephen L. Reed
 *
 * <p>Copyright 2001 Cycorp, Inc., license is open source GNU LGPL.
 * <p><a href="http://www.opencyc.org/license.txt">the license</a>
 * <p><a href="http://www.opencyc.org">www.opencyc.org</a>
 * <p><a href="http://www.sourceforge.net/projects/opencyc">OpenCyc at SourceForge</a>
 * <p>
 * THIS SOFTWARE AND KNOWLEDGE BASE CONTENT ARE PROVIDED ``AS IS'' AND
 * ANY EXPRESSED OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE OPENCYC
 * ORGANIZATION OR ITS CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE AND KNOWLEDGE
 * BASE CONTENT, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
public class UnitTest extends TestCase {

    /**
     * Creates a <tt>UnitTest</tt> object with the given name.
     */
    public UnitTest(String name) {
        super(name);
    }

    /**
     * Returns the test suite.
     *
     * @return the test suite
     */
    public static Test suite() {
        TestSuite testSuite = new TestSuite();
        testSuite.addTest(new UnitTest("testHasDuplicates"));
        testSuite.addTest(new UnitTest("testHasIntersection"));
        testSuite.addTest(new UnitTest("testRemoveDelimiters"));
        testSuite.addTest(new UnitTest("testIsDelimitedString"));
        testSuite.addTest(new UnitTest("testIsNumeric"));
        testSuite.addTest(new UnitTest("testWordsToString"));
        testSuite.addTest(new UnitTest("testEscapeDoubleQuotes"));
        return testSuite;
    }

    /**
     * Main method in case tracing is prefered over running JUnit.
     */
    public static void main(String[] args) {
        junit.textui.TestRunner.run(suite());
    }

    /**
     * Tests the OcCollectionUtils.hasDuplicates method.
     */
    public void testHasDuplicates() {
        System.out.println("\n** testHasDuplicates **");
        ArrayList collection1 = new ArrayList();
        collection1.add("a");
        collection1.add("b");
        collection1.add("b");
        Assert.assertTrue(OcCollectionUtils.hasDuplicates(collection1));
        ArrayList collection2 = new ArrayList();
        collection2.add("a");
        collection2.add("b");
        collection2.add("c");
        ArrayList arrayList = new ArrayList();
        arrayList.add("d");
        collection2.add(arrayList);
        collection2.add(arrayList);
        Assert.assertTrue(OcCollectionUtils.hasDuplicates(collection2));
        ArrayList collection3 = new ArrayList();
        collection3.add("a");
        collection3.add("b");
        collection3.add("c");
        Assert.assertTrue(! OcCollectionUtils.hasDuplicates(collection3));
        System.out.println("** testHasDuplicates OK **");
    }

    /**
     * Tests the OcCollectionUtils.hasIntersection method.
     */
    public void testHasIntersection() {
        System.out.println("\n** testHasIntersection **");
        ArrayList a = new ArrayList();
        ArrayList b = new ArrayList();
        Assert.assertTrue(! OcCollectionUtils.hasIntersection(a, b));
        a.add("a");
        Assert.assertTrue(! OcCollectionUtils.hasIntersection(a, b));
        b.add("a");
        Assert.assertTrue(OcCollectionUtils.hasIntersection(a, b));
        b.remove("a");
        Assert.assertTrue(! OcCollectionUtils.hasIntersection(a, b));
        a.add("b");
        a.add("d");
        a.add("e");
        a.add("f");
        b.add("f");
        Assert.assertTrue(OcCollectionUtils.hasIntersection(a, b));
        ArrayList bigA = new ArrayList();
        for (int i = 0; i < 150; i++)
            bigA.add(new Integer(i));
        ArrayList bigB = new ArrayList();
        for (int i = 0; i < 100; i++)
            bigB.add(new Integer(i + 200));
        Assert.assertTrue(! OcCollectionUtils.hasIntersection(bigA, bigB));
        Assert.assertTrue(! OcCollectionUtils.hasIntersection(bigB, bigA));
        Assert.assertTrue(OcCollectionUtils.hasIntersection(bigA, bigA));
        bigB.add(new Integer(10));
        Assert.assertTrue(OcCollectionUtils.hasIntersection(bigA, bigB));
        Assert.assertTrue(OcCollectionUtils.hasIntersection(bigB, bigA));
        System.out.println("** testHasIntersection OK **");
    }

    /**
     * Tests the StringUtils.removeDelimiters method.
     */
    public void testRemoveDelimiters() {
        System.out.println("\n** testRemoveDelimiters **");
        Assert.assertEquals("abc", StringUtils.removeDelimiters("\"abc\""));
        System.out.println("** testRemoveDelimiters OK**");
    }

    /**
     * Tests the StringUtils.isDelimitedString method.
     */
    public void testIsDelimitedString() {
        System.out.println("\n** testIsDelimitedString **");
        Assert.assertTrue(StringUtils.isDelimitedString("\"abc\""));
        Assert.assertTrue(StringUtils.isDelimitedString("\"\""));
        Assert.assertTrue(! StringUtils.isDelimitedString("\""));
        Assert.assertTrue(! StringUtils.isDelimitedString(new Integer(1)));
        Assert.assertTrue(! StringUtils.isDelimitedString("abc\""));
        Assert.assertTrue(! StringUtils.isDelimitedString("\"abc"));
        System.out.println("** testIsDelimitedString OK **");
    }

    /**
     * Tests the StringUtils.isNumeric method.
     */
    public void testIsNumeric() {
        System.out.println("\n** testIsNumeric **");
        Assert.assertTrue(StringUtils.isNumeric("0"));
        Assert.assertTrue(StringUtils.isNumeric("1"));
        Assert.assertTrue(StringUtils.isNumeric("2"));
        Assert.assertTrue(StringUtils.isNumeric("3"));
        Assert.assertTrue(StringUtils.isNumeric("4"));
        Assert.assertTrue(StringUtils.isNumeric("5"));
        Assert.assertTrue(StringUtils.isNumeric("6"));
        Assert.assertTrue(StringUtils.isNumeric("7"));
        Assert.assertTrue(StringUtils.isNumeric("8"));
        Assert.assertTrue(StringUtils.isNumeric("9"));
        Assert.assertTrue(! StringUtils.isNumeric("A"));
        Assert.assertTrue(! StringUtils.isNumeric("@"));
        Assert.assertTrue(! StringUtils.isNumeric("."));
        Assert.assertTrue(StringUtils.isNumeric("12345"));
        Assert.assertTrue(! StringUtils.isNumeric("123.45"));
        Assert.assertTrue(! StringUtils.isNumeric("123-45"));
        Assert.assertTrue(! StringUtils.isNumeric("12345+"));
        Assert.assertTrue(! StringUtils.isNumeric("+"));
        Assert.assertTrue(! StringUtils.isNumeric("-"));
        Assert.assertTrue(StringUtils.isNumeric("+1"));
        Assert.assertTrue(StringUtils.isNumeric("-1"));
        Assert.assertTrue(StringUtils.isNumeric("+12345"));
        Assert.assertTrue(StringUtils.isNumeric("-12345"));
        System.out.println("** testIsNumeric OK **");
    }

    /**
     * Tests the StringUtils.wordsToString method.
     */
    public void testWordsToString() {
        System.out.println("\n** testWordsToString **");
        ArrayList words = new ArrayList();
        Assert.assertEquals("", StringUtils.wordsToPhrase(words));
        words.add("word1");
        Assert.assertEquals("word1", StringUtils.wordsToPhrase(words));
        words.add("word2");
        Assert.assertEquals("word1 word2", StringUtils.wordsToPhrase(words));
        words.add("word3");
        Assert.assertEquals("word1 word2 word3", StringUtils.wordsToPhrase(words));

        System.out.println("** testWordsToString OK **");
    }

    /**
     * Tests the StringUtils.escapeDoubleQuotes method.
     */
    public void testEscapeDoubleQuotes() {
        System.out.println("\n** testEscapeDoubleQuotes **");
        String string = "";
        Assert.assertEquals(string, StringUtils.escapeDoubleQuotes(string));
        string = "1 2 3";
        Assert.assertEquals(string, StringUtils.escapeDoubleQuotes(string));
        string = "'1' '2' '3'";
        Assert.assertEquals(string, StringUtils.escapeDoubleQuotes(string));
        StringBuffer stringBuffer = new StringBuffer();
        stringBuffer.append("\"");
        stringBuffer.append("abc");
        stringBuffer.append("\"");
        string = stringBuffer.toString();
        String expectedString = "\\\"abc\\\"";
        String escapedString = StringUtils.escapeDoubleQuotes(string);
        Assert.assertEquals(expectedString, escapedString);

        System.out.println("** testEscapeDoubleQuotes OK **");
    }


}

