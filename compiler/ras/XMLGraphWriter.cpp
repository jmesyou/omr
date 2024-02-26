
#include "XMLGraphWriter.hpp"

#include <set>
#include <atomic>
#include "env/FilePointerDecl.hpp"
#include "compile/Compilation.hpp"
#include "compile/Compilation_inlines.hpp"
#include "compile/OMRCompilation.hpp"
#include "il/Node.hpp"
#include "il/Node_inlines.hpp"
#include "il/Block.hpp"
#include "il/Block_inlines.hpp"
#include "il/OMRILOps.hpp"
#include "control/OMROptions.hpp"
#include "infra/ILWalk.hpp"

void writeStringProperty(XMLGraphWriter & writer, StringProperty & property) {
  writer.getSink()->write(string_format("<p name='%s'>", property.key.c_str()));
  writer.getSink()->write(string_format("%s", property.value.c_str()));
  writer.getSink()->write("</p>\n");
}

void writeIntegerProperty(XMLGraphWriter & writer, IntegerProperty & property) {
  writer.getSink()->write(string_format("<p name='%s'>", property.name.c_str()));
  writer.getSink()->write(string_format("%d", property.value));
  writer.getSink()->write("</p>\n");
}

std::string getCategory(TR::Node * node) {
  auto opcode = node->getOpCode();

  if (opcode.isTreeTop())
    return "control";

  return "data";
}

void writeProperties(XMLGraphWriter & writer, std::vector<StringProperty> && strings, std::vector<IntegerProperty> && integers) {
  writer.getSink()->write("<properties>\n");
  for (auto s : strings) {
    writeStringProperty(writer, s);
  }
  for (auto i: integers) {
    writeIntegerProperty(writer, i);
  }
  writer.getSink()->write("</properties>\n");
}

void replace(std::string & string, std::string target, std::string replacement) {
  size_t index = 0;
  auto n = replacement.size();
  while (true) {
      /* Locate the substring to replace. */
      index = string.find(target, index);
      if (index == std::string::npos) break;

      /* Make the replacement. */
      string.replace(index, n, replacement);

      /* Advance index forward so the next iteration doesn't pick it up as well. */
      index += n;
  }
}

void sanitizeSlashes(std::string & string) {
  std::replace(string.begin(), string.end(), '/', '.');
  std::replace(string.begin(), string.end(), ';', ' ');
}

void sanitizeXML(std::string & string) {
  replace(string, "<", "&lt;");
  replace(string, ">", "&gt;");
}

std::string getNodeName(TR::Compilation * compilation, TR::Node * node) {
  std::string name = node->getOpCode().getName();

  switch (node->getOpCodeValue()) {
    case TR::iconst:
      name = string_format("%s %d", name.c_str(), node->getInt());
      break;
    case TR::lconst:
      name = string_format("%s %ld", name.c_str(), node->getLongInt());
      break;
    case TR::bconst:
      name = string_format("%s %d", name.c_str(), node->getByte());
      break;
    case TR::sconst:
      name = string_format("%s %d", name.c_str(), node->getShortInt());
      break;
    case TR::bload:
    case TR::sload:
    case TR::iload:
    case TR::lload:
    case TR::fload:
    case TR::dload:
    case TR::bstore:
    case TR::sstore:
    case TR::istore:
    case TR::fstore:
    case TR::dstore:
      name = string_format("%s %s", name.c_str(), node->getSymbolReference()->getName(compilation->getDebug()));
      break;
    default:
      return name;
  }

  sanitizeXML(name);
  return name;
}

bool XMLGraphWriter::initialize(TR::Compilation * compilation, TR::ResolvedMethodSymbol * symbol) {
  auto id = getNextAvailableCompilationId();
  auto method = symbol->getResolvedMethod();

  std::string signature = compilation->getDebug()->signature(symbol);
  std::replace(signature.begin(), signature.end(), '/', '.');
  auto hotness = compilation->getHotnessName(compilation->getMethodHotness());


  sink = new FileSink(
    string_format("TestarossaCompilation-%d[%s][%s].xml", id, signature.c_str(), hotness)
  );

  sink->write("<graphDocument>\n");
  sink->write("<group>\n");

  sanitizeXML(signature);
  writeProperties(
    *this,
    {
      {"name", signature}
    },
    {
      {"compilationId", id}
    }
  );

  return true;
}

XMLGraphWriter::XMLGraphWriter(int32_t id, TR_ResolvedMethod * method, TR::Options & options): sink(nullptr) {}

XMLGraphWriter::~XMLGraphWriter() {}

void writeNode(XMLGraphWriter & writer, TR::Compilation * compilation, TR::Node * node) {
  writer.getSink()->write(
    string_format("<node id='%d'>\n", node->getGlobalIndex())
  );

  writeProperties(
    writer,
    {
      {"name",     getNodeName(compilation, node)},
      {"category", getCategory(node)}
    },
    {
      {"idx", (int32_t) node->getGlobalIndex()}
    }
  );
  writer.getSink()->write("</node>\n");
}

void writeTreeTop(XMLGraphWriter & writer, TR::Compilation * compilation, TR::TreeTop * tt) {
  auto node = tt->getNode();
  writer.getSink()->write(
    string_format("<node id='%d'>\n", node->getGlobalIndex())
  );

  writeProperties(
    writer,
    {
      {"name",     getNodeName(compilation, node)},
      {"category", getCategory(node)}
    },
    {
      {"idx", (int32_t) node->getGlobalIndex()}
    }
  );
  writer.getSink()->write("</node>\n");
}

void writeEdge(XMLGraphWriter & writer, int32_t from, int32_t to, std::string && type, int index) {
  writer.getSink()->write(
    string_format("<edge from='%d' to='%d' type='%s' index='%d'/>\n", from, to, type.c_str(), index)
  );
}

void writeEdges(XMLGraphWriter & writer, std::vector<TR::TreeTop *> treetops, std::vector<TR::Node *> nodes) {
  writer.getSink()->write("<edges>\n");
  for (auto tt : treetops) {
    auto node = tt->getNode();
    auto globalIndex = node->getGlobalIndex();
    auto next = tt->getNextTreeTop();

    if (next)
      writeEdge(writer, node->getGlobalIndex(), next->getNode()->getGlobalIndex(), "next", 0);

    if (node->getOpCode().isBranch())
      writeEdge(writer, globalIndex, node->getBranchDestination()->getNode()->getGlobalIndex(), "branchTrue", 1);

    for (auto i = 0; i < node->getNumChildren(); i++) {
      auto child = node->getChild(i);
      writeEdge(writer, node->getGlobalIndex(), child->getGlobalIndex(), "child", i);
    }
  }

  for (auto node : nodes) {
    for (auto i = 0; i < node->getNumChildren(); i++) {
      auto child = node->getChild(i);
      writeEdge(writer, node->getGlobalIndex(), child->getGlobalIndex(), "child", i);
    }
  }
  writer.getSink()->write("</edges>");
}

void writeBlocks(XMLGraphWriter & writer, TR::Compilation * compilation, TR::CFG * cfg) {
  writer.getSink()->write("<controlFlow>\n");

  std::set<int32_t> nodeSet{};
  for (TR::AllBlockIterator iter(cfg, compilation); iter.currentBlock(); ++iter) {
    auto block = iter.currentBlock();
    auto blockNumber = block->getNumber();
    writer.getSink()->write(string_format("<block name='%d'>\n", blockNumber));

    writer.getSink()->write("<nodes>\n");
    for (TR::PreorderNodeIterator iter(block->getEntry(), compilation); iter != NULL; ++iter) {
      auto node = iter.currentNode();
      auto index = node->getGlobalIndex();
      if (nodeSet.count(index) > 0)
        continue;

      nodeSet.emplace(index);
      writer.getSink()->write(string_format("<node id='%d'/>\n", index));

      if (node->getOpCodeValue() == TR::BBEnd)
        break;
    }
    writer.getSink()->write("</nodes>\n");

    writer.getSink()->write("<successors>\n");
    TR::CFGEdgeList & successors = block->getSuccessors();
    for (auto succEdge = successors.begin(); succEdge != successors.end(); ++succEdge) {
      auto number = (*succEdge)->getTo()->getNumber();
      if (number == 1)
        continue;
      writer.getSink()->write(string_format("<successor name='%d'/>\n", number));
    }

    auto exceptionSuccessors = block->getExceptionSuccessors();
    for (auto succEdge = exceptionSuccessors.begin(); succEdge != exceptionSuccessors.end(); ++succEdge) {
      auto number = (*succEdge)->getTo()->getNumber();
      if (number == 1)
        continue;

      writer.getSink()->write(string_format("<successor name='%d'/>\n", number));
    }

    writer.getSink()->write("</successors>\n");

    writer.getSink()->write("</block>\n");
  }
  writer.getSink()->write("</controlFlow>\n");
}

void XMLGraphWriter::writeGraph(std::string & string, TR::Compilation * compilation, TR::ResolvedMethodSymbol * symbol) {
  if (!initialized)
    initialized = initialize(compilation, symbol);

  sink->write(
    string_format("<graph name = '%s'>\n", string.c_str())
  );

  writeProperties(*this, {}, {});

  int32_t nodes_count = 0;
  auto firstTreeTop = symbol->getFirstTreeTop();

  std::vector<TR::Node *> nodes{};
  std::vector<TR::TreeTop *> treetops{};
  for (TR::TreeTopIterator ttCursor (symbol->getFirstTreeTop(), compilation); ttCursor != NULL; ++ttCursor) {
    TR::TreeTop * tt = ttCursor.currentTree();
    nodes.push_back(tt->getNode());
    treetops.push_back(tt);
  }


  for (TR::PreorderNodeIterator iter (symbol->getFirstTreeTop(), compilation); iter != NULL; ++iter) {
    auto node = iter.currentNode();
    nodes.push_back(node);
  }

  sink->write("<nodes>\n");
  for (auto tt: treetops) {
    writeTreeTop(*this, compilation, tt);
  }

  for (auto node: nodes) {
    writeNode(*this, compilation, node);
  }
  sink->write("</nodes>\n");

  writeEdges(*this, treetops, nodes);

  writeBlocks(*this, compilation, symbol->getFlowGraph());
  sink->write("</graph>\n");
}


