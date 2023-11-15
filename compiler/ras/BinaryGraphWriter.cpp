/*******************************************************************************
 * Copyright IBM Corp. and others 2000
 *
 * This program and the accompanying materials are made available under
 * the terms of the Eclipse Public License 2.0 which accompanies this
 * distribution and is available at http://eclipse.org/legal/epl-2.0
 * or the Apache License, Version 2.0 which accompanies this distribution
 * and is available at https://www.apache.org/licenses/LICENSE-2.0.
 *
 * This Source Code may also be made available under the following Secondary
 * Licenses when the conditions for such availability set forth in the
 * Eclipse Public License, v. 2.0 are satisfied: GNU General Public License,
 * version 2 with the GNU Classpath Exception [1] and GNU General Public
 * License, version 2 with the OpenJDK Assembly Exception [2].
 *
 * [1] https://www.gnu.org/software/classpath/license.html
 * [2] https://openjdk.org/legal/assembly-exception.html
 *
 * SPDX-License-Identifier: EPL-2.0 OR Apache-2.0 OR GPL-2.0 WITH Classpath-exception-2.0 OR LicenseRef-GPL-2.0 WITH Assembly-exception
 *******************************************************************************/

#include "BinaryGraphWriter.hpp"
#include <stdio.h>
#include <string>
#include <tuple>
#include <utility>
#include <vector>
#include <cstdlib>
#include <atomic>
#include <algorithm>
#include "env/FilePointerDecl.hpp"
#include "compile/Compilation.hpp"
#include "compile/Compilation_inlines.hpp"
#include "il/Node.hpp"
#include "il/Block.hpp"
#include "il/Block_inlines.hpp"
#include "il/Node_inlines.hpp"
#include "il/OMRILOps.hpp"
#include "control/OMROptions.hpp"
#include "infra/ILWalk.hpp"

const uint32_t BUFFER_SIZE_BYTES = 4096;

const int8_t BEGIN_GRAPH = 0x1;

const int8_t BEGIN_GROUP = 0;
const int8_t CLOSE_GROUP = 0x2;
const int8_t PROPERTY_POOL = 0x0;

const int8_t POOL_NEW = 0x0;
const int8_t POOL_STRING = 0x1;
const int8_t POOL_ENUM = 0x2;
const int8_t POOL_CLASS = 0x3;
const int8_t POOL_METHOD = 0x4;
const int8_t POOL_NULL = 0x5;
const int8_t POOL_NODE_CLASS = 0x6;
const int8_t POOL_SIGNATURE = 0x8;

const int8_t KLASS = 0x0;
const int8_t ENUM_KLASS = 0x1;

static PoolEnumClass InputEdgeType = {"InputEdgeType", {"values"}};

void BinaryGraphWriter::writeStringProperty(StringProperty & property) {
  TR_ASSERT(property.key != nullptr, "String property key cannot be null");
  TR_ASSERT(property.value != nullptr, "String property value cannot be null");
  writePoolString(property.key);
  writePropString(property.value);
}

void writeNodeProperties(BinaryGraphWriter & writer, TR::ILOpCodes opcode);
void writeGraphBody(BinaryGraphWriter& writer, std::string & name, TR::ResolvedMethodSymbol * methodSymbol);
void writeBlock(BinaryGraphWriter & writer, TR::Block * block);
void writeNode(BinaryGraphWriter& writer, TR::Node * node);
void writeNodeClass(BinaryGraphWriter& writer, TR::Node * node);
void writeTreeTop(BinaryGraphWriter& writer, TR::TreeTop * node);
void writeTreeTopClass(BinaryGraphWriter& writer, TR::TreeTop * node);

BinaryGraphWriter::BinaryGraphWriter(TR::Compilation * comp):
  _comp(comp),
  _file(NULL),
  buffer(),
  initialized(false),
  stringPool(),
  poolId(0) {}

BinaryGraphWriter::~BinaryGraphWriter() {
  if (initialized) {
    writeInt8(CLOSE_GROUP);
    flushBuffer(/*force*/ true);
    fclose(_file);
  }
}

void BinaryGraphWriter::initialize(TR::ResolvedMethodSymbol * symbol) {
  if (initialized)
    return;

  auto _symbol = symbol;
  if (!_symbol)
     _symbol = _comp->getMethodSymbol();

  TR_ASSERT(_symbol != NULL, "method symbol during debug cannot be null!");

  auto id = 0;
  auto method = _symbol->getResolvedMethod();

  std::string signature = _comp->getDebug()->signature(symbol);
  std::replace(signature.begin(), signature.end(), '/', '.');
  if (_file == NULL) {
    auto hotness = _comp->getHotnessName(_comp->getMethodHotness());

    std::string filename = string_format("TestarossaCompilation-%d[%s][%s].bgv", id, signature.c_str(), hotness);
    _file = fopen(filename.c_str(), "wb");

    if (_file == NULL) {
      throw std::runtime_error("Error opening file");
    }
  }

  writeInt8('B');
  writeInt8('I');
  writeInt8('G');
  writeInt8('V');

  const int8_t MAJOR = 7;
  const int8_t MINOR = 0;
  writeInt8(MAJOR);
  writeInt8(MINOR);

  writeInt8(BEGIN_GROUP);

  std::string name = string_format("%d:%s", id, signature.c_str());
  writePoolString(name);
  writePoolString("Placeholder short name");
  writePoolMethod(symbol);

  writeInt32(0);

  std::string graphType = string_format("StructuredGraph:%d{TestarossaCompilation<%s>}", id, signature.c_str());
  std::vector<StringProperty> properties{
    {"graph", graphType}
  };
  writeProperties(properties);

  initialized = true;
}

void BinaryGraphWriter::writeInt8(int8_t byte) {
  flushBuffer(/*force*/ false);
  buffer.emplace_back(byte);
}

void BinaryGraphWriter::writeGraph(char * title, TR::ResolvedMethodSymbol * methodSymbol) {
  initialize(methodSymbol);
  writeInt8(BEGIN_GRAPH);
  // id
  auto optIndex = getCompilation()->getOptIndex();
  writeInt32(optIndex);

  writeString(title);

  // arg_count
  writeInt32(0);

  std::string name = title;
  writeGraphBody(* this, name, methodSymbol);
}

void BinaryGraphWriter::writeInt16(int16_t n) {
  static const uint8_t mask = 0xFF;
  writeInt8((n >> 8) & mask);
  writeInt8(n & mask);
}

void BinaryGraphWriter::writeInt32(int32_t n) {
  static const int8_t mask = 0xFF;
  for (int i = 3; i >= 0; i--) {
    int8_t byte = (n >> i*8) & mask;
    writeInt8(byte);
  }
}

void BinaryGraphWriter::writeString(std::string str) {
  auto c_str = str.c_str();
  int32_t len = str.size();
  writeInt32(len);
  for (int i = 0; i < len; i++) {
    writeInt8(c_str[i]);
  }
}

void writeGraphBody(BinaryGraphWriter& writer, std::string & name, TR::ResolvedMethodSymbol * methodSymbol) {
  // props
  std::vector<StringProperty> stringProperties{
    {"label", name}
  };
  writer.writeProperties(stringProperties);

  auto comp = writer.getCompilation();
  int32_t nodes_count = 0;
  auto firstTreeTop = methodSymbol->getFirstTreeTop();
  for (TR::TreeTopIterator ttCursor (methodSymbol->getFirstTreeTop(), comp); ttCursor != NULL; ++ttCursor) {
    nodes_count++;
  }

  for (TR::PreorderNodeIterator iter (methodSymbol->getFirstTreeTop(), comp); iter != NULL; ++iter) {
    if (!iter.currentNode()->getOpCode().isTreeTop())
      nodes_count++;
  }
  writer.writeInt32(nodes_count);

  auto written_nodes = 0;
  for (TR::TreeTopIterator ttCursor (methodSymbol->getFirstTreeTop(), comp); ttCursor != NULL; ++ttCursor) {
    TR::TreeTop * tt = ttCursor.currentTree();
    writeTreeTop(writer, tt);
  }

  for (TR::PreorderNodeIterator iter (methodSymbol->getFirstTreeTop(), comp); iter != NULL; ++iter) {
    auto node = iter.currentNode();
    if (!node->getOpCode().isTreeTop())
      writeNode(writer, node);
  }

  // block_count
  // TODO: encode block information
  writer.writeInt32(0);
  /*
  auto cfg = methodSymbol->getFlowGraph();
  writer.writeInt32(cfg->getNumberOfNodes());
  for (TR::AllBlockIterator iter(cfg, writer.getCompilation()); iter.currentBlock(); ++iter) {
    writeBlock(writer, iter.currentBlock());
  }
  */
}


std::vector<InputEdgeInfo> getInputEdgeInfo(TR::Node * node) {
  auto edges = std::vector<InputEdgeInfo>();

  for (auto i = 0; i < node->getNumChildren(); ++i) {
    InputEdgeInfo info = {
      false,
      string_format("value[%d]", i),
      0
    };
    edges.emplace_back(info);
  }

  return edges;
}

std::vector<OutputEdgeInfo> getOutputEdgeInfo(TR::TreeTop * treetop) {
  auto node = treetop->getNode();
  auto opcode = node->getOpCode();

  if (opcode.isBranch()) {
    return {
      {false, "falseBranch"},
      {false, "trueBranch"}
    };
  }

  if (treetop->getNextTreeTop() != NULL)
    return {{false, "nextTreeTop"}};

  return {};
}

std::vector<StringProperty> getNodeProperties(BinaryGraphWriter & writer, TR::Node * node) {
  switch (node->getOpCodeValue()) {
    case TR::BBStart:
      return {{"category", "begin"}};
    case TR::BBEnd:
       return {{"category", "end"}};
    case TR::Return:
    case TR::areturn:
    case TR::ireturn:
    case TR::freturn:
    case TR::lreturn:
    case TR::dreturn:
       return {{"category", "controlSink"}};
    case TR::acall:
    case TR::icall:
    case TR::lcall:
    case TR::dcall:
    case TR::fcall:
    case TR::call:
      return {
        {"category", "floating"},
        {"target", node->getSymbolReference()->getName(writer.getCompilation()->getDebug())}
      };
    case TR::astore:
    case TR::istore:
    case TR::fstore:
    case TR::lstore:
    case TR::dstore:
      return {
        {"category", "fixed"},
        {"destination", node->getSymbolReference()->getName(writer.getCompilation()->getDebug())}
      };
    case TR::aload:
    case TR::iload:
    case TR::fload:
    case TR::lload:
    case TR::dload:
      return {
        {"category", "floating"},
        {"destination", node->getSymbolReference()->getName(writer.getCompilation()->getDebug())}
      };
    case TR::iconst:
    case TR::lconst:
    case TR::sconst:
    case TR::bconst:
      return {
        {"category", "floating"},
        {"rawvalue", std::to_string(node->getLongInt())},
        {"datatype", node->getDataType().toString()}
      };
    default: {
      auto opcode = node->getOpCode();
      if (opcode.isBranch())
        return {{"category", "controlSplit"}};
      else if (opcode.isTreeTop())
        return {{"category", "fixed"}};

      return {{"category", "floating"}};
      }
  }
}

std::string getNameFormat(TR::Node * node) {
  switch (node->getOpCodeValue()) {
    case TR::iadd:
    case TR::dadd:
    case TR::ladd:
    case TR::sadd:
    case TR::badd:
      return "+";
    case TR::ishl:
    case TR::lshl:
    case TR::sshl:
    case TR::bshl:
      return "<<";
    case TR::ifacmpeq:
    case TR::ifbcmpeq:
    case TR::ifdcmpeq:
    case TR::iffcmpeq:
    case TR::ificmpeq:
    case TR::iflcmpeq:
    case TR::ifscmpeq:
      return "If ==";
    case TR::ifacmpne:
    case TR::ifbcmpne:
    case TR::ifdcmpne:
    case TR::iffcmpne:
    case TR::ificmpne:
    case TR::iflcmpne:
    case TR::ifscmpne:
      return "If !=";
    case TR::ifacmplt:
    case TR::ifbcmplt:
    case TR::ifdcmplt:
    case TR::iffcmplt:
    case TR::ificmplt:
    case TR::ifiucmplt:
    case TR::iflucmplt:
    case TR::iflcmplt:
    case TR::ifscmplt:
      return "If <";
    case TR::ifacmple:
    case TR::ifbcmple:
    case TR::ifdcmple:
    case TR::iffcmple:
    case TR::ificmple:
    case TR::ifiucmple:
    case TR::iflucmple:
    case TR::iflcmple:
    case TR::ifscmple:
      return "If <=";
    case TR::ifacmpgt:
    case TR::ifbcmpgt:
    case TR::ifdcmpgt:
    case TR::iffcmpgt:
    case TR::ificmpgt:
    case TR::ifiucmpgt:
    case TR::iflucmpgt:
    case TR::iflcmpgt:
    case TR::ifscmpgt:
      return "If >";
    case TR::ifacmpge:
    case TR::ifbcmpge:
    case TR::ifdcmpge:
    case TR::iffcmpge:
    case TR::ificmpge:
    case TR::ifiucmpge:
    case TR::iflucmpge:
    case TR::iflcmpge:
    case TR::ifscmpge:
      return "If >=";
    case TR::acall:
    case TR::icall:
    case TR::lcall:
    case TR::dcall:
    case TR::fcall:
    case TR::call:
      return "Call {p#target}";
    case TR::Return:
    case TR::areturn:
    case TR::ireturn:
    case TR::freturn:
    case TR::lreturn:
    case TR::dreturn:
      return "Return";
    case TR::astore:
    case TR::istore:
    case TR::fstore:
    case TR::lstore:
    case TR::dstore:
      return "Store {p#destination}";
    case TR::aload:
    case TR::iload:
    case TR::fload:
    case TR::lload:
    case TR::dload:
      return "Load {p#destination}";
    case TR::iconst:
    case TR::lconst:
    case TR::bconst:
    case TR::sconst:
      return "C({p#rawvalue}) {p#datatype}";
    default:
      return node->getOpCode().getName();
  }
}



void writeBlock(BinaryGraphWriter & writer, TR::Block * block) {
  // id
  writer.writeInt32(block->getNumber());

  std::vector<TR::Node *> nodes{};
  auto comp = writer.getCompilation();
  for (TR::PreorderNodeIterator iter(block->getEntry(), comp); iter != block->getExit(); ++iter) {
    TR::Node *node = iter.currentNode();
    nodes.emplace_back(node);
  }
  // node_count
  writer.writeInt32(nodes.size());
  for (auto node : nodes) {
    writer.writeInt32(node->getGlobalIndex());
  }

  TR::CFGEdgeList & successors = block->getSuccessors();
  // follower_count
  writer.writeInt32(successors.size());
  for (auto succEdge = successors.begin(); succEdge != successors.end(); ++succEdge) {
    writer.writeInt32((*succEdge)->getTo()->getNumber());
  }
}

void writeTreeTop(BinaryGraphWriter & writer, TR::TreeTop * tt) {
  TR::Node * node = tt->getNode();
  writer.writeInt32(node->getGlobalIndex());

  writer.writePoolNodeClass(tt->getNode(), getInputEdgeInfo(tt->getNode()), getOutputEdgeInfo(tt));

  int8_t hasPredecessor = tt->getPrevRealTreeTop() ? 1 : 0;
  writer.writeInt8(hasPredecessor);

  auto properties = getNodeProperties(writer, tt->getNode());
  writer.writeProperties(properties);

  for (auto iter = node->childIterator(); iter.currentChild() != NULL; ++iter) {
    writer.writeDirectEdge(iter->getGlobalIndex());
  }

  if (node->getOpCode().isBranch())
    writer.writeDirectEdge(node->getBranchDestination()->getNode()->getGlobalIndex());

  auto next = tt->getNextTreeTop();
  if (next != NULL)
    writer.writeDirectEdge(next->getNode()->getGlobalIndex());
  /*
  auto next = tt->getNextTreeTop();
  if (next)
    writeDirectEdge(next->getNode()->getGlobalIndex());
  */
}

void writeNode(BinaryGraphWriter & writer, TR::Node * node) {
  writer.writeInt32(node->getGlobalIndex());

  writer.writePoolNodeClass(node, getInputEdgeInfo(node), {});

  // non tree top nodes should never have a predecessor
  writer.writeInt8(0);

  // props
  auto properties = getNodeProperties(writer, node);
  writer.writeProperties(properties);

  for (auto iter = node->childIterator(); iter.currentChild() != NULL; ++iter) {
    writer.writeDirectEdge(iter->getGlobalIndex());
  }
}

void BinaryGraphWriter::writePropString(std::string string) {
  writeInt8(PROPERTY_POOL);
  writePoolString(string);
}

void BinaryGraphWriter::writePoolString(std::string string) {
  if (stringPool.contains(string))  {
    writePoolReference(POOL_STRING, stringPool.lookup(string));
    return;
  }

  writeInt8(POOL_NEW);
  uint16_t id = stringPool.insert(string, getNextPoolId());
  writeInt16(id);
  writeInt8(POOL_STRING);
  writeString(string);
}

void BinaryGraphWriter::writePoolReference(int8_t type, uint16_t id) {
  writeInt8(type);
  writeInt16(id);
}

void BinaryGraphWriter::writeInputEdgeInfo(InputEdgeInfo info) {
  writeInt8(info.isIndirect);
  writePoolString(info.name);
  writePoolEnumValue(0, InputEdgeType);
}

void BinaryGraphWriter::writeOutputEdgeInfo(OutputEdgeInfo info) {
  writeInt8(info.isIndirect);
  writePoolString(info.name);
}

void BinaryGraphWriter::writePoolEnumValue(uint32_t index, PoolEnumClass cls) {
  if (enumValuePool.contains(std::forward_as_tuple(cls.name, index))) {
    auto id = enumValuePool.lookup(std::forward_as_tuple(cls.name, index));
    writePoolReference(POOL_ENUM, id);
    return;
  }

  auto id = getNextPoolId();
  writeInt8(POOL_NEW);
  writeInt16(id);

  writeInt8(POOL_ENUM);
  writePoolEnumClass(cls);
  writeInt32(index);

  enumValuePool.insert(std::forward_as_tuple(cls.name, index), id);
}

void BinaryGraphWriter::writePoolNodeClass(TR::Node * node, std::vector<InputEdgeInfo> && inputs, std::vector<OutputEdgeInfo> && outputs) {
  auto name = node->getOpCode().getName();
  auto edges_in = inputs.size();
  auto edges_out = outputs.size();
  if (nodeClassPool.contains(std::forward_as_tuple(name, edges_in, edges_out)))  {
    writePoolReference(POOL_NODE_CLASS, nodeClassPool.lookup(std::forward_as_tuple(name, edges_in, edges_out)));
    return;
  }

  /**
  struct {
    sint8 type = POOL_NODE_CLASS
    PoolObject node_class
    String name_template
    sint16 input_count
    InputEdgeInfo[input_count] inputs
    sint16 output_count
    OutputEdgeInfo[output_count] outputs
  }
  */
  writeInt8(POOL_NEW);
  uint16_t id = nodeClassPool.insert(std::forward_as_tuple(name, edges_in, edges_out), getNextPoolId());
  writeInt16(id);
  writeInt8(POOL_NODE_CLASS);
  // node_class
  writePoolClass(name);
  // name_template
  writeString(getNameFormat(node));

  writeInt16(edges_in);
  for (int i = 0; i < edges_in; i++) {
    auto edge = inputs.at(i);
    writeInputEdgeInfo(edge);
  }

  writeInt16(edges_out);
  for (int i = 0; i < edges_out; i++) {
    auto edge = outputs.at(i);
    writeOutputEdgeInfo(edge);
  }
}

void BinaryGraphWriter::writeDirectEdge(int32_t node_id) {
  writeInt32(node_id);
}

void BinaryGraphWriter::flushBuffer(bool force) {
  if (buffer.size() < BUFFER_SIZE_BYTES && !force) {
    return;
  }
  if (fwrite(buffer.data(), sizeof(int8_t), buffer.size(), _file) < buffer.size()) {
    throw std::runtime_error("failed to flush byte buffer during graph writing");
  }
  fflush(_file);
  buffer.clear();
}

uint16_t BinaryGraphWriter::getNextPoolId() {
  return poolId++;
}

TR::Compilation * BinaryGraphWriter::getCompilation() {
  return _comp;
}

void BinaryGraphWriter::writeProperties(std::vector<StringProperty> & stringProperties) {
  auto size = stringProperties.size();
  writeInt16(size);
  for (auto property: stringProperties) {
    writeStringProperty(property);
  }
}

void BinaryGraphWriter::writePoolMethod(TR::ResolvedMethodSymbol * symbol) {
  auto signature = _comp->getDebug()->signature(symbol);
  if (methodPool.contains(signature)) {
    auto id = methodPool.lookup(signature);
    writePoolReference(POOL_METHOD, id);
    return;
  }
  auto id  = 0;
  writeInt8(POOL_NEW);
  writeInt16(id);

  writeInt8(POOL_METHOD);
  auto resolvedMethod = symbol->getResolvedMethod();
  // auto classSymbol = _comp->getSymRefTab()->findOrCreateClassSymbol(symbol, -1, resolvedMethod->classOfMethod());

  auto className = resolvedMethod->classNameChars();
  writePoolClass(className);

  auto methodName = symbol->getResolvedMethod()->nameChars();
  writePoolString(methodName);
  writePoolSignature(symbol);

  writeInt32(symbol->getFlags());
  auto maxByteCodeIndex = resolvedMethod->maxBytecodeIndex();
  writeInt32(maxByteCodeIndex);

  auto bytecode = resolvedMethod->bytecodeStart();
  for (int i = 0; i < maxByteCodeIndex; i++) {
    writeInt8(bytecode[i]);
  }
}

void BinaryGraphWriter::writePoolSignature(TR::ResolvedMethodSymbol *symbol) {
  auto resolvedMethod = symbol->getResolvedMethod();
  std::string signature = resolvedMethod->signatureChars();

  if (signaturePool.contains(signature)) {
    auto id = signaturePool.lookup(signature);
    writePoolReference(POOL_SIGNATURE, id);
    return;
  }

  auto id = getNextPoolId();
  writeInt8(POOL_NEW);
  writeInt16(id);

  writeInt8(POOL_SIGNATURE);

  writeInt16(resolvedMethod->numberOfParameters());

  for (int i = 0; i < resolvedMethod->numberOfParameters(); i++) {
    writePoolString(resolvedMethod->parmType(i).toString());
  }

  writePoolString(resolvedMethod->returnType().toString());

  signaturePool.insert(signature, id);
}

void BinaryGraphWriter::writePoolClass(std::string name) {
  if (typePool.contains(name)) {
    auto id = typePool.lookup(name);
    writePoolReference(POOL_CLASS, id);
    return;
  }

  auto id = getNextPoolId();
  writeInt8(POOL_NEW);
  writeInt16(id);

  writeInt8(POOL_CLASS);
  writeString(name);
  writeInt8(KLASS);

  typePool.insert(name, id);
  return;
}

void BinaryGraphWriter::writePoolEnumClass(PoolEnumClass cls) {
  auto name = cls.name;
  if (typePool.contains(name)) {
    auto id = typePool.lookup(name);
    writePoolReference(POOL_CLASS, id);
    return;
  }

  auto id = getNextPoolId();
  writeInt8(POOL_NEW);
  writeInt16(id);

  writeInt8(POOL_CLASS);
  writeString(name);
  writeInt8(ENUM_KLASS);

  writeInt32(cls.values.size());
  for (auto v : cls.values) {
    writePoolString(v);
  }

  typePool.insert(name, id);
  return;
}
