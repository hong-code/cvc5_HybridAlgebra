/******************************************************************************
 * Top contributors (to current version):
 *   Andrew Reynolds
 *
 * This file is part of the cvc5 project.
 *
 * Copyright (c) 2009-2024 by the authors listed in the file AUTHORS
 * in the top-level source directory and their institutional affiliations.
 * All rights reserved.  See the file COPYING in the top-level source
 * directory for licensing information.
 * ****************************************************************************
 *
 * A proof generator for lemmas that use ProofRule::ARITH_MULT_ABS_COMPARISON.
 */

#include "theory/arith/nl/ext/arith_nl_compare_proof_gen.h"

#include "expr/attribute.h"
#include "proof/proof.h"
#include "theory/arith/arith_utilities.h"
#include "theory/arith/nl/ext/monomial_check.h"

namespace cvc5::internal {
namespace theory {
namespace arith {
namespace nl {

ArithNlCompareProofGenerator::ArithNlCompareProofGenerator(Env& env)
    : EnvObj(env)
{
}

ArithNlCompareProofGenerator::~ArithNlCompareProofGenerator() {}

std::shared_ptr<ProofNode> ArithNlCompareProofGenerator::getProofFor(Node fact)
{
  Assert(fact.getKind() == Kind::IMPLIES);
  std::vector<Node> exp;
  if (fact[0].getKind() == Kind::AND)
  {
    exp.insert(exp.end(), fact[0].begin(), fact[0].end());
  }
  else
  {
    exp.emplace_back(fact[0]);
  }
  Node conc = fact[1];
  Trace("arith-nl-compare")
      << "Comparsion prove: " << exp << " => " << conc << std::endl;
  // get the expected form of the literals
  CDProof cdp(d_env);
  std::vector<Node> expc;
  std::map<Node, Node> deq;
  for (const Node& e : exp)
  {
    Node ec = getCompareLit(e);
    if (ec.isNull())
    {
      // not a comparison literal, likely a disequality to zero
      Node v = isDisequalZero(e);
      Assert(!v.isNull());
      deq[v] = e;
      continue;
    }
    expc.emplace_back(ec);
    // a comparsion literal
    if (e != ec)
    {
      Node eeq = e.eqNode(ec);
      Node eeqSym = ec.eqNode(e);
      cdp.addTrustedStep(
          eeqSym, TrustId::ARITH_NL_COMPARE_LIT_TRANSFORM, {}, {});
      cdp.addStep(ec, ProofRule::EQ_RESOLVE, {e, eeq}, {});
    }
    // add to product
    Assert(ec.getNumChildren() == 2);
  }
  // reorder the explanation based on the order it appears in the conclusion
  Node concc = getCompareLit(conc);
  Assert(!concc.isNull());
  Assert(concc.getNumChildren() == 2);
  Assert(concc[0].getKind() == Kind::ABS);
  std::vector<Node> cprod[2];
  Kind ck = decomposeCompareLit(concc, cprod[0], cprod[1]);
  // convert to exponent counts
  std::map<Node, size_t> mexp[2];
  for (size_t i = 0; i < 2; i++)
  {
    std::vector<Node>& cpi = cprod[i];
    std::map<Node, size_t>& mi = mexp[i];
    for (const Node& p : cpi)
    {
      mi[p]++;
    }
  }
  // if GT conclusion, ensure the first explanation is GT, which makes checking
  // simpler
  if (ck == Kind::GT)
  {
    for (size_t i = 0, nexp = expc.size(); i < nexp; i++)
    {
      if (expc[i].getKind() == Kind::GT)
      {
        if (i > 0)
        {
          Node tmp = expc[i];
          expc[i] = expc[0];
          expc[0] = tmp;
        }
        break;
      }
    }
  }
  std::vector<size_t> eexp;
  // reorder the conclusion based on the explanation
  NodeManager* nm = nodeManager();
  std::vector<Node> cprodt[2];
  for (const Node& e : expc)
  {
    std::vector<Node> eprod[2];
    decomposeCompareLit(e, eprod[0], eprod[1]);
    Assert(eprod[0].size() <= 1 && eprod[1].size() <= 1);
    for (size_t i = 0; i < 2; i++)
    {
      if (eprod[i].empty())
      {
        size_t ii = 1 - i;
        Node a = eprod[ii][0];
        size_t na = mexp[ii][a];
        size_t nb = mexp[i][a];
        // Don't take more than this side has. This handles cases like
        // (> (abs x) (abs 1)) => (> (abs (* x x)) (abs x)),
        // where we should only consume one copy of x.
        size_t n = na - nb;
        Node one = mkOne(a.getType());
        for (size_t j = 0; j < n; j++)
        {
          cprodt[i].push_back(one);
          cprodt[ii].push_back(a);
        }
        mexp[ii][a] -= n;
        eexp.emplace_back(n);
        break;
      }
      else if (i == 1)
      {
        // both non-empty, take min
        // Note that in theory it is possible to construct a lemma where this
        // would be incorrect, e.g. x>a ^ x>b ^ y>a => xxy > aab, although
        // lemmas of this form are not generated. In particular, we consider
        // premises where monomials on RHS/LHS occur in consecutive premises,
        // as they are ordered by model value in the MonomialCheck solver.
        Node a = eprod[0][0];
        Node b = eprod[1][0];
        size_t na = mexp[0][a];
        size_t nb = mexp[1][b];
        size_t n = na > nb ? nb : na;
        for (size_t j = 0; j < 2; j++)
        {
          const Node& c = eprod[j][0];
          for (size_t k = 0; k < n; k++)
          {
            cprodt[j].push_back(c);
          }
          mexp[j][c] -= n;
        }
        eexp.emplace_back(n);
      }
    }
  }
  // now get the leftover factors, one by one
  for (const std::pair<const Node, size_t>& m : mexp[0])
  {
    if (m.second > 0)
    {
      for (size_t k = 0; k < m.second; k++)
      {
        cprodt[0].push_back(m.first);
        cprodt[1].push_back(m.first);
      }
      Node v = nm->mkNode(Kind::ABS, m.first);
      Node veq = v.eqNode(v);
      cdp.addStep(veq, ProofRule::REFL, {}, {v});
      expc.emplace_back(veq);
      eexp.push_back(m.second);
    }
  }
  // if strict version, we go back and guard zeroes
  if (ck == Kind::GT)
  {
    std::map<Node, Node>::iterator itd;
    for (size_t i = 0, nexp = expc.size(); i < nexp; i++)
    {
      Node e = expc[i];
      if (e.getKind() != ck)
      {
        // needs to have a disequal to zero explanation
        std::vector<Node> eprod[2];
        decomposeCompareLit(e, eprod[0], eprod[1]);
        if (eprod[0].size() != 1)
        {
          Assert(false) << "ArithNlCompareProofGenerator failed explain";
          return nullptr;
        }
        itd = deq.find(eprod[0][0]);
        if (itd == deq.end())
        {
          Assert(false) << "ArithNlCompareProofGenerator failed explain deq";
          return nullptr;
        }
        Node guardEq = nm->mkNode(Kind::AND, e, itd->second);
        cdp.addStep(guardEq, ProofRule::AND_INTRO, {e, itd->second}, {});
        expc[i] = guardEq;
      }
    }
  }
  Assert(eexp.size() == expc.size());
  // use repetition of explanation to match the exponents
  std::vector<Node> expcFinal;
  for (size_t i = 0, nexp = expc.size(); i < nexp; i++)
  {
    size_t n = eexp[i];
    for (size_t j = 0; j < n; j++)
    {
      expcFinal.emplace_back(expc[i]);
    }
  }
  Node opa = mkProduct(nm, cprodt[0]);
  Node opb = mkProduct(nm, cprodt[1]);
  Node newConc = mkLit(nm, ck, opa, opb);
  Trace("arith-nl-compare")
      << "...processed prove: " << expc << " => " << concc << std::endl;
  Trace("arith-nl-compare")
      << "...grouped conclusion is " << newConc << std::endl;
  cdp.addStep(newConc, ProofRule::ARITH_MULT_ABS_COMPARISON, expcFinal, {});
  // the grouped literal should be equivalent by rewriting
  if (newConc != concc)
  {
    cdp.addStep(concc, ProofRule::MACRO_SR_PRED_TRANSFORM, {newConc}, {concc});
  }
  if (conc != concc)
  {
    Node ceq = concc.eqNode(conc);
    cdp.addTrustedStep(ceq, TrustId::ARITH_NL_COMPARE_LIT_TRANSFORM, {}, {});
    cdp.addStep(conc, ProofRule::EQ_RESOLVE, {concc, ceq}, {});
  }
  cdp.addStep(fact, ProofRule::SCOPE, {conc}, exp);
  return cdp.getProofFor(fact);
}

std::string ArithNlCompareProofGenerator::identify() const
{
  return "ArithNlCompareProofGenerator";
}

Node ArithNlCompareProofGenerator::mkProduct(NodeManager* nm,
                                             const std::vector<Node>& c)
{
  Assert(!c.empty());
  return c.size() == 1 ? c[0] : nm->mkNode(Kind::NONLINEAR_MULT, c);
}

Node ArithNlCompareProofGenerator::mkLit(NodeManager* nm,
                                         Kind k,
                                         const Node& a,
                                         const Node& b)
{
  Assert(a.getType() == b.getType());
  // add absolute value
  Node au = nm->mkNode(Kind::ABS, a);
  Node bu = nm->mkNode(Kind::ABS, b);
  return nm->mkNode(k, au, bu);
}

struct ArithNlCompareLitAttributeId
{
};

using ArithNlCompareLitAttribute =
    expr::Attribute<ArithNlCompareLitAttributeId, Node>;

void ArithNlCompareProofGenerator::setCompareLit(
    NodeManager* nm, Node olit, Kind k, const Node& a, const Node& b)
{
  Node lit = mkLit(nm, k, a, b);
  ArithNlCompareLitAttribute ancla;
  olit.setAttribute(ancla, lit);
}

Node ArithNlCompareProofGenerator::getCompareLit(const Node& olit)
{
  ArithNlCompareLitAttribute ancla;
  return olit.getAttribute(ancla);
}

Kind ArithNlCompareProofGenerator::decomposeCompareLit(const Node& lit,
                                                       std::vector<Node>& a,
                                                       std::vector<Node>& b)
{
  Kind k = lit.getKind();
  if (k != Kind::EQUAL && k != Kind::GT)
  {
    return Kind::UNDEFINED_KIND;
  }
  if (lit[0].getKind() != Kind::ABS || lit[1].getKind() != Kind::ABS)
  {
    return Kind::UNDEFINED_KIND;
  }
  addProduct(lit[0][0], a);
  addProduct(lit[1][0], b);
  return k;
}

void ArithNlCompareProofGenerator::addProduct(const Node& n,
                                              std::vector<Node>& vec)
{
  if (n.getKind() == Kind::NONLINEAR_MULT)
  {
    vec.insert(vec.end(), n.begin(), n.end());
  }
  else if (n.isConst() && n.getConst<Rational>().isOne())
  {
    // do nothing
  }
  else
  {
    vec.emplace_back(n);
  }
}

Node ArithNlCompareProofGenerator::isDisequalZero(const Node& g)
{
  if (g.getKind() == Kind::NOT && g[0].getKind() == Kind::EQUAL
      && g[0][1].isConst() && g[0][1].getConst<Rational>().isZero())
  {
    return g[0][0];
  }
  return Node::null();
}

}  // namespace nl
}  // namespace arith
}  // namespace theory
}  // namespace cvc5::internal