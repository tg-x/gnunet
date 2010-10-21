//
// Generated by JTB 1.3.2
//

package org.gnunet.seaspider.parser.nodes;

/**
 * Grammar production:
 * <PRE>
 * f0 -> "*"
 * f1 -> [ &lt;CONST&gt; ]
 * f2 -> [ Pointer() ]
 * </PRE>
 */
public class Pointer implements Node {
   public NodeToken f0;
   public NodeOptional f1;
   public NodeOptional f2;

   public Pointer(NodeToken n0, NodeOptional n1, NodeOptional n2) {
      f0 = n0;
      f1 = n1;
      f2 = n2;
   }

   public Pointer(NodeOptional n0, NodeOptional n1) {
      f0 = new NodeToken("*");
      f1 = n0;
      f2 = n1;
   }

   public void accept(org.gnunet.seaspider.parser.visitors.Visitor v) {
      v.visit(this);
   }
   public <R,A> R accept(org.gnunet.seaspider.parser.visitors.GJVisitor<R,A> v, A argu) {
      return v.visit(this,argu);
   }
   public <R> R accept(org.gnunet.seaspider.parser.visitors.GJNoArguVisitor<R> v) {
      return v.visit(this);
   }
   public <A> void accept(org.gnunet.seaspider.parser.visitors.GJVoidVisitor<A> v, A argu) {
      v.visit(this,argu);
   }
}

