//
// Generated by JTB 1.3.2
//

package org.gnunet.seaspider.parser.nodes;

/**
 * The interface which NodeList, NodeListOptional, and NodeSequence
 * implement.
 */
public interface NodeListInterface extends Node {
   public void addNode(Node n);
   public Node elementAt(int i);
   public java.util.Enumeration<Node> elements();
   public int size();

   public void accept(org.gnunet.seaspider.parser.visitors.Visitor v);
   public <R,A> R accept(org.gnunet.seaspider.parser.visitors.GJVisitor<R,A> v, A argu);
   public <R> R accept(org.gnunet.seaspider.parser.visitors.GJNoArguVisitor<R> v);
   public <A> void accept(org.gnunet.seaspider.parser.visitors.GJVoidVisitor<A> v, A argu);
}

