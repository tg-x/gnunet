//
// Generated by JTB 1.3.2
//

package org.gnunet.seaspider.parser.nodes;

/**
 * Grammar production:
 * <PRE>
 * f0 -> ( &lt;AUTO&gt; | &lt;REGISTER&gt; )*
 * </PRE>
 */
public class VariableClassSpecifier implements Node {
   public NodeListOptional f0;

   public VariableClassSpecifier(NodeListOptional n0) {
      f0 = n0;
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

