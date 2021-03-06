//===-- AttributeImpl.h - Attribute Internals -------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// \brief This file defines various helper methods and classes used by
/// LLVMContextImpl for creating and managing attributes.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_IR_ATTRIBUTEIMPL_H
#define LLVM_LIB_IR_ATTRIBUTEIMPL_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/FoldingSet.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/IR/Attributes.h"
#include "llvm/Support/TrailingObjects.h"
#include <algorithm>
#include <cassert>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>

namespace llvm {

class LLVMContext;

//===----------------------------------------------------------------------===//
/// \class
/// \brief This class represents a single, uniqued attribute. That attribute
/// could be a single enum, a tuple, or a string.
class AttributeImpl : public FoldingSetNode {
  unsigned char KindID; ///< Holds the AttrEntryKind of the attribute

protected:
  enum AttrEntryKind {
    EnumAttrEntry,
    IntAttrEntry,
    StringAttrEntry
  };

  AttributeImpl(AttrEntryKind KindID) : KindID(KindID) {}

public:
  // AttributesImpl is uniqued, these should not be available.
  AttributeImpl(const AttributeImpl &) = delete;
  AttributeImpl &operator=(const AttributeImpl &) = delete;

  virtual ~AttributeImpl();

  bool isEnumAttribute() const { return KindID == EnumAttrEntry; }
  bool isIntAttribute() const { return KindID == IntAttrEntry; }
  bool isStringAttribute() const { return KindID == StringAttrEntry; }

  bool hasAttribute(Attribute::AttrKind A) const;
  bool hasAttribute(StringRef Kind) const;

  Attribute::AttrKind getKindAsEnum() const;
  uint64_t getValueAsInt() const;

  StringRef getKindAsString() const;
  StringRef getValueAsString() const;

  /// \brief Used when sorting the attributes.
  bool operator<(const AttributeImpl &AI) const;

  void Profile(FoldingSetNodeID &ID) const {
    if (isEnumAttribute())
      Profile(ID, getKindAsEnum(), 0);
    else if (isIntAttribute())
      Profile(ID, getKindAsEnum(), getValueAsInt());
    else
      Profile(ID, getKindAsString(), getValueAsString());
  }
  static void Profile(FoldingSetNodeID &ID, Attribute::AttrKind Kind,
                      uint64_t Val) {
    ID.AddInteger(Kind);
    if (Val) ID.AddInteger(Val);
  }
  static void Profile(FoldingSetNodeID &ID, StringRef Kind, StringRef Values) {
    ID.AddString(Kind);
    if (!Values.empty()) ID.AddString(Values);
  }
};

//===----------------------------------------------------------------------===//
/// \class
/// \brief A set of classes that contain the value of the
/// attribute object. There are three main categories: enum attribute entries,
/// represented by Attribute::AttrKind; alignment attribute entries; and string
/// attribute enties, which are for target-dependent attributes.

class EnumAttributeImpl : public AttributeImpl {
  virtual void anchor();
  Attribute::AttrKind Kind;

protected:
  EnumAttributeImpl(AttrEntryKind ID, Attribute::AttrKind Kind)
      : AttributeImpl(ID), Kind(Kind) {}

public:
  EnumAttributeImpl(Attribute::AttrKind Kind)
      : AttributeImpl(EnumAttrEntry), Kind(Kind) {}

  Attribute::AttrKind getEnumKind() const { return Kind; }
};

class IntAttributeImpl : public EnumAttributeImpl {
  void anchor() override;
  uint64_t Val;

public:
  IntAttributeImpl(Attribute::AttrKind Kind, uint64_t Val)
      : EnumAttributeImpl(IntAttrEntry, Kind), Val(Val) {
    assert((Kind == Attribute::Alignment || Kind == Attribute::StackAlignment ||
            Kind == Attribute::Dereferenceable ||
            Kind == Attribute::DereferenceableOrNull ||
            Kind == Attribute::AllocSize) &&
           "Wrong kind for int attribute!");
  }

  uint64_t getValue() const { return Val; }
};

class StringAttributeImpl : public AttributeImpl {
  virtual void anchor();
  std::string Kind;
  std::string Val;

public:
  StringAttributeImpl(StringRef Kind, StringRef Val = StringRef())
      : AttributeImpl(StringAttrEntry), Kind(Kind), Val(Val) {}

  StringRef getStringKind() const { return Kind; }
  StringRef getStringValue() const { return Val; }
};

//===----------------------------------------------------------------------===//
/// \class
/// \brief This class represents a group of attributes that apply to one
/// element: function, return type, or parameter.
class AttributeSetNode final
    : public FoldingSetNode,
      private TrailingObjects<AttributeSetNode, Attribute> {
  friend TrailingObjects;

  /// Bitset with a bit for each available attribute Attribute::AttrKind.
  uint64_t AvailableAttrs;
  unsigned NumAttrs; ///< Number of attributes in this node.

  AttributeSetNode(ArrayRef<Attribute> Attrs);

public:
  // AttributesSetNode is uniqued, these should not be available.
  AttributeSetNode(const AttributeSetNode &) = delete;
  AttributeSetNode &operator=(const AttributeSetNode &) = delete;

  void operator delete(void *p) { ::operator delete(p); }

  static AttributeSetNode *get(LLVMContext &C, const AttrBuilder &B);

  static AttributeSetNode *get(LLVMContext &C, ArrayRef<Attribute> Attrs);

  /// \brief Return the number of attributes this AttributeList contains.
  unsigned getNumAttributes() const { return NumAttrs; }

  bool hasAttribute(Attribute::AttrKind Kind) const {
    return AvailableAttrs & ((uint64_t)1) << Kind;
  }
  bool hasAttribute(StringRef Kind) const;
  bool hasAttributes() const { return NumAttrs != 0; }

  Attribute getAttribute(Attribute::AttrKind Kind) const;
  Attribute getAttribute(StringRef Kind) const;

  unsigned getAlignment() const;
  unsigned getStackAlignment() const;
  uint64_t getDereferenceableBytes() const;
  uint64_t getDereferenceableOrNullBytes() const;
  std::pair<unsigned, Optional<unsigned>> getAllocSizeArgs() const;
  std::string getAsString(bool InAttrGrp) const;

  typedef const Attribute *iterator;
  iterator begin() const { return getTrailingObjects<Attribute>(); }
  iterator end() const { return begin() + NumAttrs; }

  void Profile(FoldingSetNodeID &ID) const {
    Profile(ID, makeArrayRef(begin(), end()));
  }
  static void Profile(FoldingSetNodeID &ID, ArrayRef<Attribute> AttrList) {
    for (const auto &Attr : AttrList)
      Attr.Profile(ID);
  }
};

typedef std::pair<unsigned, AttributeSet> IndexAttrPair;

//===----------------------------------------------------------------------===//
/// \class
/// \brief This class represents a set of attributes that apply to the function,
/// return type, and parameters.
class AttributeListImpl final
    : public FoldingSetNode,
      private TrailingObjects<AttributeListImpl, IndexAttrPair> {
  friend class AttributeList;
  friend TrailingObjects;

private:
  LLVMContext &Context;
  unsigned NumSlots; ///< Number of entries in this set.
  /// Bitset with a bit for each available attribute Attribute::AttrKind.
  uint64_t AvailableFunctionAttrs;

  // Helper fn for TrailingObjects class.
  size_t numTrailingObjects(OverloadToken<IndexAttrPair>) { return NumSlots; }

  /// \brief Return a pointer to the IndexAttrPair for the specified slot.
  const IndexAttrPair *getSlotPair(unsigned Slot) const {
    return getTrailingObjects<IndexAttrPair>() + Slot;
  }

public:
  AttributeListImpl(LLVMContext &C,
                    ArrayRef<std::pair<unsigned, AttributeSet>> Slots);

  // AttributesSetImpt is uniqued, these should not be available.
  AttributeListImpl(const AttributeListImpl &) = delete;
  AttributeListImpl &operator=(const AttributeListImpl &) = delete;

  void operator delete(void *p) { ::operator delete(p); }

  /// \brief Get the context that created this AttributeListImpl.
  LLVMContext &getContext() { return Context; }

  /// \brief Return the number of slots used in this attribute list. This is
  /// the number of arguments that have an attribute set on them (including the
  /// function itself).
  unsigned getNumSlots() const { return NumSlots; }

  /// \brief Get the index of the given "slot" in the AttrNodes list. This index
  /// is the index of the return, parameter, or function object that the
  /// attributes are applied to, not the index into the AttrNodes list where the
  /// attributes reside.
  unsigned getSlotIndex(unsigned Slot) const {
    return getSlotPair(Slot)->first;
  }

  /// \brief Retrieve the attribute set node for the given "slot" in the
  /// AttrNode list.
  AttributeSet getSlotAttributes(unsigned Slot) const {
    return getSlotPair(Slot)->second;
  }

  /// \brief Return true if the AttributeSet or the FunctionIndex has an
  /// enum attribute of the given kind.
  bool hasFnAttribute(Attribute::AttrKind Kind) const {
    return AvailableFunctionAttrs & ((uint64_t)1) << Kind;
  }

  typedef AttributeSet::iterator iterator;
  iterator begin(unsigned Slot) const {
    return getSlotAttributes(Slot).begin();
  }
  iterator end(unsigned Slot) const { return getSlotAttributes(Slot).end(); }

  void Profile(FoldingSetNodeID &ID) const;
  static void Profile(FoldingSetNodeID &ID,
                      ArrayRef<std::pair<unsigned, AttributeSet>> Nodes);

  void dump() const;
};

} // end namespace llvm

#endif // LLVM_LIB_IR_ATTRIBUTEIMPL_H
