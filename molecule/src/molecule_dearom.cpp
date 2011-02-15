/****************************************************************************
 * Copyright (C) 2009-2011 GGA Software Services LLC
 * 
 * This file is part of Indigo toolkit.
 * 
 * This file may be distributed and/or modified under the terms of the
 * GNU General Public License version 3 as published by the Free Software
 * Foundation and appearing in the file LICENSE.GPL included in the
 * packaging of this file.
 * 
 * This file is provided AS IS with NO WARRANTY OF ANY KIND, INCLUDING THE
 * WARRANTY OF DESIGN, MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 ***************************************************************************/

#include "molecule/query_molecule.h"
#include "molecule/molecule_dearom.h"
#include "base_c/bitarray.h"
#include "base_cpp/output.h"
#include "base_cpp/scanner.h"
#include "graph/filter.h"
#include "molecule/molecule.h"
#include "molecule/molecule_arom.h"
#include "molecule/elements.h"

using namespace indigo;

static int _dearomatizationParams = Dearomatizer::PARAMS_SAVE_ONE_DEAROMATIZATION;

Dearomatizer::Dearomatizer (BaseMolecule &molecule, const int *atom_external_conn) :
   _graphMatching(molecule), _molecule(molecule), _aromaticGroups(molecule),
   TL_CP_GET(_aromaticGroupData),
   //TL_CP_GET(_edgesFixed),
   //TL_CP_GET(_verticesFixed),
   TL_CP_GET(_submoleculeMapping)
{
   _edgesFixed.resize(_molecule.edgeEnd());
   _verticesFixed.resize(_molecule.vertexEnd());
   _verticesFixed.zeroFill();

   _connectivityGroups = _aromaticGroups.detectAromaticGroups(atom_external_conn);

   _initVertices();
   _initEdges();

   _graphMatching.setFixedInfo(&_edgesFixed, &_verticesFixed);
}

Dearomatizer::~Dearomatizer ()
{
}

void Dearomatizer::setDearomatizationParams (int params)
{
   _dearomatizationParams = params;
}

// Enumerate all dearomatizations for all connectivity groups
void Dearomatizer::enumerateDearomatizations (DearomatizationsStorage &dearomatizations)
{
   dearomatizations.clear();
   if (_connectivityGroups == 0)
      return;
   _dearomatizations = &dearomatizations;

   QS_DEF(Molecule, submolecule);

   dearomatizations.setGroupsCount(_connectivityGroups);
   dearomatizations.setDearomatizationParams(_dearomatizationParams);

   _aromaticGroups.constructGroups(dearomatizations, true);

   for (int group = 0; group < _connectivityGroups; group++)
   {
      _activeGroup = group;
      _prepareGroup(group, submolecule);

      GrayCodesEnumerator grayCodes(_aromaticGroupData.heteroAtoms.size(), true);
      do
      {
         if (_graphMatching.findMatching())
            _processMatching(submolecule, group, grayCodes.getCode());

         grayCodes.next();

         if (!grayCodes.isDone())
         {
            int heteroAtomToInvert = _aromaticGroupData.heteroAtoms[grayCodes.getBitChangeIndex()];
            _fixHeteratom(heteroAtomToInvert, !_verticesFixed.get(heteroAtomToInvert));
         }
      } while (!grayCodes.isDone());
   }
}

void Dearomatizer::_fixHeteratom (int atom_idx, bool toFix)
{
   
   if (!_verticesFixed.get(atom_idx))
   {
      if (_graphMatching.isVertexInMatching(atom_idx))
         _graphMatching.removeVertexFromMatching(atom_idx);

      _verticesFixed.set(atom_idx);
   } 
   else
      _verticesFixed.reset(atom_idx);
   return;
}

void Dearomatizer::_initVertices (void)
{
   for (int v_idx = _molecule.vertexBegin(); 
      v_idx < _molecule.vertexEnd(); 
      v_idx = _molecule.vertexNext(v_idx))
   {
      if (_molecule.getAtomAromaticity(v_idx) == ATOM_ALIPHATIC)
         _verticesFixed.set(v_idx);
   }
}

// Find all aromatic bonds
void Dearomatizer::_initEdges (void)
{
   for (int e_idx = _molecule.edgeBegin(); 
      e_idx < _molecule.edgeEnd(); 
      e_idx = _molecule.edgeNext(e_idx))
   {
      _edgesFixed.set(e_idx, _molecule.getBondOrder(e_idx) != BOND_AROMATIC);
   }
}

void Dearomatizer::_enumerateMatching (void)
{
   // Find strong edge in alternating circle
   const Edge *edge = 0;
   int e_idx;
   bool found = false;
   for (int i = 0; i < _aromaticGroupData.bonds.size(); i++)
   {
      e_idx = _aromaticGroupData.bonds[i];
      if (!_edgesFixed.get(e_idx) && _graphMatching.isEdgeMatching(e_idx))
      {
         edge = &(_molecule.getEdge(e_idx));

         if (_graphMatching.findAlternatingPath(edge->beg, edge->end, false, false))
         {
            found = true;
            break;
         }
      }
   }
   if (!found)
   {
      _handleMatching();
      return;
   }

   const int MAX_PATH_SIZE = 100;
   int pathSize = _graphMatching.getPathSize();
   int path[MAX_PATH_SIZE];
   memcpy(path, _graphMatching.getPath(), sizeof(int) * pathSize);

   // Enumerate all matching with this strong edge
   _verticesFixed.set(edge->beg);
   _verticesFixed.set(edge->end);
   _enumerateMatching();
   _verticesFixed.reset(edge->beg);
   _verticesFixed.reset(edge->end);

   // Enumerate all matching without this strong edge

   _graphMatching.setPath(path, pathSize);
   _graphMatching.setEdgeMatching(e_idx, false);
   _graphMatching.processPath();
   _edgesFixed.set(e_idx);

   _enumerateMatching();

   _edgesFixed.reset(e_idx);
   _graphMatching.setPath(path, pathSize);
   _graphMatching.processPath();
   _graphMatching.setEdgeMatching(e_idx, true);
}

void Dearomatizer::_handleMatching (void)
{
   // Add dearomatizations
   _dearomatizations->addGroupDearomatization(_activeGroup,
      _graphMatching.getEdgesState());
}

void Dearomatizer::_processMatching (Molecule &submolecule, int group, 
                                     const byte* hetroAtomsState)
{
   // Copy bonds
   for (int e_idx = submolecule.edgeBegin(); 
      e_idx < submolecule.edgeEnd(); 
      e_idx = submolecule.edgeNext(e_idx))
   {
      const Edge &edge = submolecule.getEdge(e_idx);
      int supIdx = _molecule.findEdgeIndex(_submoleculeMapping[edge.beg], 
         _submoleculeMapping[edge.end]);

      if (_graphMatching.isEdgeMatching(supIdx))
         submolecule.setBondOrder(e_idx, BOND_DOUBLE);
      else
         submolecule.setBondOrder(e_idx, BOND_SINGLE);
   }

   // Check aromaticity
   MoleculeAromatizer::aromatizeBonds(submolecule);
   bool isAromatic = true;
   for (int e_idx = submolecule.edgeBegin(); 
      e_idx < submolecule.edgeEnd(); 
      e_idx = submolecule.edgeNext(e_idx))
   {
      if (submolecule.getBondOrder(e_idx) != BOND_AROMATIC)
      {
         isAromatic = false;
         break;
      }
   }

   if (isAromatic)
   {
      if (_dearomatizationParams == PARAMS_SAVE_ALL_DEAROMATIZATIONS) 
         // Enumerate all equivalent dearomatizations
         _enumerateMatching();
      else if (_dearomatizationParams == PARAMS_SAVE_ONE_DEAROMATIZATION) 
         _handleMatching();
      else if (_dearomatizationParams == PARAMS_SAVE_JUST_HETERATOMS) 
         _dearomatizations->addGroupHeteroAtomsState(group, hetroAtomsState);
   }
}

void Dearomatizer::_prepareGroup (int group, Molecule &submolecule)
{
   _aromaticGroups.getGroupData(group, DearomatizationsGroups::GET_VERTICES_FILTER | 
      DearomatizationsGroups::GET_HETERATOMS_INDICES, &_aromaticGroupData);

   Filter filter(_aromaticGroupData.verticesFilter.ptr(), Filter::EQ, 1);
   submolecule.makeSubmolecule(_molecule, filter, &_submoleculeMapping, NULL, SKIP_ALL);
   // Rremove aromatic bonds
   for (int e_idx = submolecule.edgeBegin(); 
      e_idx < submolecule.edgeEnd(); 
      e_idx = submolecule.edgeNext(e_idx))
   {
      if (submolecule.getBondOrder(e_idx) != BOND_AROMATIC)
         submolecule.removeEdge(e_idx);
   }

   for (int i = 0; i < _aromaticGroupData.vertices.size(); i++)
   {
      int v_idx = _aromaticGroupData.vertices[i];
      if (!_aromaticGroups.isAcceptDoubleBond(v_idx))
         _verticesFixed.set(v_idx);
      else
         _verticesFixed.reset(v_idx);
   }
   for (int i = 0; i < _aromaticGroupData.heteroAtoms.size(); i++)
   {
      int hetero_idx = _aromaticGroupData.heteroAtoms[i];
      _verticesFixed.set(hetero_idx);
   }

   _graphMatching.reset();
   _graphMatching.setEdgesMappingPtr(_aromaticGroupData.bondsInvMapping.ptr());
   _graphMatching.setVerticesSetPtr(_aromaticGroupData.vertices.ptr(), 
      _aromaticGroupData.vertices.size());
}

//
// Dearomatizer::DearomatizerGraphMatching
//

bool Dearomatizer::GraphMatchingFixed::checkVertex (int v_idx)
{
   return !_verticesFixed->get(v_idx);
}

bool Dearomatizer::GraphMatchingFixed::checkEdge (int e_idx)
{
   return !_edgesFixed->get(e_idx);
}

void Dearomatizer::GraphMatchingFixed::setFixedInfo (
   const Dbitset *edgesFixed, const Dbitset *verticesFixed)
{
   _edgesFixed = edgesFixed;
   _verticesFixed = verticesFixed;
}

Dearomatizer::GraphMatchingFixed::GraphMatchingFixed (BaseMolecule &molecule) :
   GraphPerfectMatching(molecule, USE_VERTICES_SET | USE_EDGES_MAPPING)
{

}

//
// Dearomatizations
//

void DearomatizationsStorage::clear (void)
{
   _heteroAtomsStateArray.clear();
   _aromaticGroups.clear();
   clearIndices();
   clearBondsState();
   _dearomParams = Dearomatizer::PARAMS_NO_DEAROMATIZATIONS;
}        

void DearomatizationsStorage::clearIndices (void)
{
   _aromBondsArray.clear();
   _heteroAtomsIndicesArray.clear();
}

void DearomatizationsStorage::clearBondsState (void)
{
   _dearomBondsStateArray.clear();
   for (int i = 0; i < _aromaticGroups.size(); i++)
   {
      _aromaticGroups[i].dearomBondsState.count = 0;
      _aromaticGroups[i].dearomBondsState.offset = 0;
   }
}

void DearomatizationsStorage::setGroupsCount (int groupsCount)
{
   _aromaticGroups.resize(groupsCount);
   _aromaticGroups.zerofill();
}

void DearomatizationsStorage::setGroup (int group, int boundsCount, const int *bondsPtr, 
                                 int heteroAtomsCount, const int *hetroAtoms)
{
   _aromaticGroups[group].aromBondsIndices.count = boundsCount;
   _aromaticGroups[group].aromBondsIndices.offset = _aromBondsArray.size();

   if (_dearomParams == Dearomatizer::PARAMS_SAVE_JUST_HETERATOMS)
   {
      _aromaticGroups[group].heteroAtomsIndices.count  = heteroAtomsCount;
      _aromaticGroups[group].heteroAtomsIndices.offset = _heteroAtomsIndicesArray.size();
      for (int i = 0; i < heteroAtomsCount; i++)
         _heteroAtomsIndicesArray.push(hetroAtoms[i]);
   }
   else 
   {
      _aromaticGroups[group].heteroAtomsIndices.count  = 0;
      _aromaticGroups[group].heteroAtomsIndices.offset = _heteroAtomsIndicesArray.size();
   }

   for (int i = 0; i < boundsCount; i++)
      _aromBondsArray.push(bondsPtr[i]);
}

void DearomatizationsStorage::addGroupDearomatization (int group, const byte *dearomBondsState)
{
   // Check group 
   int dearomStateSize = bitGetSize(_aromaticGroups[group].aromBondsIndices.count);
   int expectedOffset = _dearomBondsStateArray.size() - 
      dearomStateSize * _aromaticGroups[group].dearomBondsState.count;

   if (_aromaticGroups[group].dearomBondsState.count != 0 && 
         _aromaticGroups[group].dearomBondsState.offset != expectedOffset)
      throw Error("Dearomatizations::addGroupDearomatization: unable to add dearomatization");

   if (_aromaticGroups[group].dearomBondsState.count == 0)
      _aromaticGroups[group].dearomBondsState.offset = _dearomBondsStateArray.size();

   // Add dearomatization to group         
   for (int i = 0; i < dearomStateSize; i++)
      _dearomBondsStateArray.push(dearomBondsState[i]);
   _aromaticGroups[group].dearomBondsState.count++;
}

void DearomatizationsStorage::addGroupHeteroAtomsState (int group, const byte *heteroAtomsState)
{
   // Check group 
   int heteroAtomsSize = bitGetSize(_aromaticGroups[group].heteroAtomsIndices.count);
   int expectedOffset = _heteroAtomsStateArray.size() - 
      heteroAtomsSize * _aromaticGroups[group].heteroAtomsState.count;

   if (_aromaticGroups[group].heteroAtomsState.count != 0 && 
         _aromaticGroups[group].heteroAtomsState.offset != expectedOffset)
      throw Error("Dearomatizations::addGroupHeteroAtomsState: unable to add heteroatoms state");

   if (_aromaticGroups[group].heteroAtomsState.count == 0)
      _aromaticGroups[group].heteroAtomsState.offset = _heteroAtomsStateArray.size();

   for (int i = 0; i < heteroAtomsSize; i++)
      _heteroAtomsStateArray.push(heteroAtomsState[i]);
   _aromaticGroups[group].heteroAtomsState.count++;
}

// Bonds state for dearomatization
int DearomatizationsStorage::getGroupDearomatizationsCount (int group) const
{
   return _aromaticGroups[group].dearomBondsState.count;
}

byte* DearomatizationsStorage::getGroupDearomatization (int group, int dearomatizationIndex)
{
   int offset = _aromaticGroups[group].dearomBondsState.offset +  
      dearomatizationIndex * bitGetSize(_aromaticGroups[group].aromBondsIndices.count);
   return &_dearomBondsStateArray[offset];
}

const int* DearomatizationsStorage::getGroupBonds (int group) const
{
   return &_aromBondsArray[_aromaticGroups[group].aromBondsIndices.offset];
}

int DearomatizationsStorage::getGroupBondsCount (int group) const
{
   return _aromaticGroups[group].aromBondsIndices.count;
}

int DearomatizationsStorage::getGroupsCount (void) const
{
   return _aromaticGroups.size();
}

// Heteroatoms
int DearomatizationsStorage::getGroupHeterAtomsStateCount (int group) const
{
   return _aromaticGroups[group].heteroAtomsState.count;
}

const byte* DearomatizationsStorage::getGroupHeterAtomsState (int group, int index) const
{
   int offset = _aromaticGroups[group].heteroAtomsState.offset +  
      index * bitGetSize(_aromaticGroups[group].heteroAtomsIndices.count);
   return _heteroAtomsStateArray.ptr() + offset;
}

const int* DearomatizationsStorage::getGroupHeteroAtoms (int group) const
{
   return _heteroAtomsIndicesArray.ptr() + _aromaticGroups[group].heteroAtomsIndices.offset;
}

int DearomatizationsStorage::getGroupHeteroAtomsCount (int group) const
{
   return _aromaticGroups[group].heteroAtomsIndices.count;
}

// I/O
void DearomatizationsStorage::saveBinary (Output &output) const
{
   output.writeByte(_dearomParams);
   output.writePackedShort(_aromaticGroups.size());
   if (_dearomParams != Dearomatizer::PARAMS_SAVE_JUST_HETERATOMS)
   {
      for (int i = 0; i < _aromaticGroups.size(); i++)
      {
         int expectedOffset = 0;
         if (i != 0)
            expectedOffset = _aromaticGroups[i - 1].dearomBondsState.offset + 
               _aromaticGroups[i - 1].dearomBondsState.count * 
               bitGetSize(_aromaticGroups[i - 1].aromBondsIndices.count);
         if (i != 0 && _aromaticGroups[i].dearomBondsState.offset != expectedOffset)
            throw Error("DearomatizationsStorage::saveBinary: invalid data order #1");
         output.writePackedShort(_aromaticGroups[i].dearomBondsState.count);
      }
      output.writePackedShort(_dearomBondsStateArray.size());
      if (_dearomBondsStateArray.size() != 0)
         output.write(_dearomBondsStateArray.ptr(), _dearomBondsStateArray.size() * sizeof(byte));
   }
   else
   {
      for (int i = 0; i < _aromaticGroups.size(); i++)
      {
         int expectedOffset = 0;
         if (i != 0)
            expectedOffset = _aromaticGroups[i - 1].heteroAtomsState.offset + 
               _aromaticGroups[i - 1].heteroAtomsState.count *
               bitGetSize(_aromaticGroups[i - 1].heteroAtomsIndices.count);
         if (i != 0 && _aromaticGroups[i].heteroAtomsState.offset != expectedOffset)
            throw Error("DearomatizationsStorage::saveBinary: invalid data order #2");
         output.writePackedShort(_aromaticGroups[i].heteroAtomsState.count);
      }

      output.writePackedShort(_heteroAtomsStateArray.size());
      if (_heteroAtomsStateArray.size() != 0)
         output.write(_heteroAtomsStateArray.ptr(), _heteroAtomsStateArray.size() * sizeof(byte));
   }
}

void DearomatizationsStorage::loadBinary (Scanner &scanner)
{
   clear();

   _dearomParams = scanner.readChar();
   short groupsCount = scanner.readPackedShort();
   _aromaticGroups.resize(groupsCount);
   _aromaticGroups.zerofill();

   if (_dearomParams != Dearomatizer::PARAMS_SAVE_JUST_HETERATOMS)
   {
      for (int i = 0; i < groupsCount; i++)
      {
         short count = scanner.readPackedShort();
         if (i != 0)
            _aromaticGroups[i].dearomBondsState.offset = 
               _aromaticGroups[i - 1].dearomBondsState.offset + count;
         _aromaticGroups[i].dearomBondsState.count = count;
      }
      short bondsStateSize = scanner.readPackedShort();
      _dearomBondsStateArray.resize(bondsStateSize);
      if (bondsStateSize != 0)
         scanner.read(bondsStateSize, _dearomBondsStateArray.ptr());
   }
   else 
   {
      for (int i = 0; i < groupsCount; i++)
      {
         short count = scanner.readPackedShort();
         if (i != 0)
            _aromaticGroups[i].heteroAtomsState.offset = 
            _aromaticGroups[i - 1].heteroAtomsState.offset + count;
         _aromaticGroups[i].heteroAtomsState.count = count;
      }

      short heteroAtomsStateSize = scanner.readPackedShort();
      _heteroAtomsStateArray.resize(heteroAtomsStateSize);
      if (heteroAtomsStateSize)
         scanner.read(heteroAtomsStateSize, _heteroAtomsStateArray.ptr());
   }
}

DearomatizationsStorage::DearomatizationsStorage (void)
{
   _dearomParams = Dearomatizer::PARAMS_NO_DEAROMATIZATIONS;
}


//
// DearomatizationsGroups
//

DearomatizationsGroups::DearomatizationsGroups (BaseMolecule &molecule) :
   _molecule(molecule),
   TL_CP_GET(_vertexAromaticGroupIndex),
   TL_CP_GET(_vertexIsAcceptDoubleEdge),
   TL_CP_GET(_vertexProcessed),
   TL_CP_GET(_groupVertices),
   TL_CP_GET(_groupEdges),
   TL_CP_GET(_groupHeteroAtoms),
   TL_CP_GET(_groupData)
{
}


void DearomatizationsGroups::getGroupData (int group, int flags, 
         DearomatizationsGroups::GROUP_DATA *data)
{
   data->bonds.clear();
   data->bondsInvMapping.resize(_molecule.edgeEnd());
   data->heteroAtoms.clear();
   data->vertices.clear();

   if (flags & GET_VERTICES_FILTER)
   {
      data->verticesFilter.resize(_molecule.vertexEnd());
      data->verticesFilter.zerofill();
   }
   for (int v_idx = _molecule.vertexBegin(); 
      v_idx < _molecule.vertexEnd(); 
      v_idx = _molecule.vertexNext(v_idx))
   {
      if (_vertexAromaticGroupIndex[v_idx] != group)
         continue;

      data->vertices.push(v_idx);
      if (flags & GET_VERTICES_FILTER)
         data->verticesFilter[v_idx] = 1;

      if (flags & GET_HETERATOMS_INDICES)
      {
         // Check if atom have lone pair or vacant orbital 
         int lonepairs;

         int label = _molecule.getAtomNumber(v_idx);
         int charge = _molecule.getAtomCharge(v_idx);
         int radical = _molecule.getAtomRadical(v_idx);

         // Treat unset charge and radical as zero;
         // We have checked before in detectAromaticGroups()
         if (charge == CHARGE_UNKNOWN)
            charge = 0;
         if (radical == -1)
            radical = 0;

         if (label == -1)
            throw DearomatizationMatcher::Error("internal error");

         int max_conn = Element::getMaximumConnectivity(label, 
            charge, radical, false);

         int group = Element::group(_molecule.getAtomNumber(v_idx));

         int vac = _molecule.getVacantPiOrbitals(group, charge, radical, max_conn, &lonepairs);

         if (_vertexIsAcceptDoubleEdge[v_idx] && (vac > 0 || lonepairs > 0)) 
            data->heteroAtoms.push(v_idx);
      }
   }

   memset(data->bondsInvMapping.ptr(), -1, sizeof(int) * data->bondsInvMapping.size());
   for (int e_idx = _molecule.edgeBegin(); 
      e_idx < _molecule.edgeEnd(); 
      e_idx = _molecule.edgeNext(e_idx))
   {
      const Edge &edge = _molecule.getEdge(e_idx);
      int bond_order = _molecule.getBondOrder(e_idx);

      if (bond_order == BOND_AROMATIC && _vertexAromaticGroupIndex[edge.beg] == group)
      {
         data->bonds.push(e_idx);
         data->bondsInvMapping[e_idx] = data->bonds.size() - 1;
      }
   }
}

// Construct bondsInvMapping, vertices and heteroAtomsInvMapping
void DearomatizationsGroups::getGroupDataFromStorage (DearomatizationsStorage &storage, 
                                                      int group, GROUP_DATA *data)
{
   data->bondsInvMapping.resize(_molecule.edgeEnd());
   data->vertices.clear();
   data->heteroAtomsInvMapping.resize(_molecule.vertexEnd());
   _vertexProcessed.resize(_molecule.vertexEnd());
   _vertexProcessed.zerofill();

   memset(data->bondsInvMapping.ptr(), -1, sizeof(int) * data->bondsInvMapping.size());
   memset(data->heteroAtomsInvMapping.ptr(), -1, sizeof(int) * data->heteroAtomsInvMapping.size());

   int bondsCount = storage.getGroupBondsCount(group);
   const int *bonds = storage.getGroupBonds(group);
   for (int i = 0; i < bondsCount; i++)
   {
      int e_idx = bonds[i];
      data->bondsInvMapping[e_idx] = i;
      const Edge &edge = _molecule.getEdge(e_idx);

      if (!_vertexProcessed[edge.beg])
      {
         data->vertices.push(edge.beg);
         _vertexProcessed[edge.beg] = 1;
      }
      if (!_vertexProcessed[edge.end])
      {
         data->vertices.push(edge.end);
         _vertexProcessed[edge.end] = 1;
      }
   }

   int heteroAtomsCount = storage.getGroupHeteroAtomsCount(group);
   const int *heteroAtoms = storage.getGroupHeteroAtoms(group);
   for (int i = 0; i < heteroAtomsCount; i++)
   {
      int h_idx = heteroAtoms[i];
      data->heteroAtomsInvMapping[h_idx] = i;
   }
}

int DearomatizationsGroups::detectAromaticGroups (const int *atom_external_conn)
{
   _vertexAromaticGroupIndex.resize(_molecule.vertexEnd());
   _vertexIsAcceptDoubleEdge.resize(_molecule.vertexEnd());
   memset(_vertexAromaticGroupIndex.ptr(), -1, _vertexAromaticGroupIndex.size() * sizeof(int));

   int currentAromaticGroup = 0;

   QueryMolecule *qmol = 0;

   if (_molecule.isQueryMolecule())
      qmol = &_molecule.asQueryMolecule();

   for (int v_idx = _molecule.vertexBegin(); 
      v_idx < _molecule.vertexEnd(); 
      v_idx = _molecule.vertexNext(v_idx))
   {
      if (_vertexAromaticGroupIndex[v_idx] != -1)
         continue;

      if ((_molecule.getAtomAromaticity(v_idx) == ATOM_ALIPHATIC) || _molecule.isPseudoAtom(v_idx))
         continue;

      if (_molecule.getAtomNumber(v_idx) == -1)
         continue;

      if (qmol != 0 && qmol->getAtom(v_idx).hasConstraint(QueryMolecule::ATOM_CHARGE) &&
          qmol->getAtomCharge(v_idx) == CHARGE_UNKNOWN)
         continue;

      if (qmol != 0 && qmol->getAtom(v_idx).hasConstraint(QueryMolecule::ATOM_RADICAL) &&
          qmol->getAtomCharge(v_idx) == -1)
         continue;

      _vertexAromaticGroupIndex[v_idx] = currentAromaticGroup++;
      _detectAromaticGroups(v_idx, atom_external_conn);
   }

   _aromaticGroups = currentAromaticGroup;
   return _aromaticGroups;
}

// Construct group structure in DearomatizationsStorage
void DearomatizationsGroups::constructGroups (DearomatizationsStorage &storage, 
                                              bool needHeteroAtoms)
{
   if (storage.getGroupsCount() == 0 && _aromaticGroups != 0)
      storage.setGroupsCount(_aromaticGroups);
   storage.clearIndices();

   for (int group = 0; group < _aromaticGroups; group++)
   {
      int flags = 0;
      if (needHeteroAtoms)
         flags = GET_HETERATOMS_INDICES;
      getGroupData(group, flags, &_groupData);
      storage.setGroup(group, _groupData.bonds.size(), _groupData.bonds.ptr(), 
         _groupData.heteroAtoms.size(), _groupData.heteroAtoms.ptr());
   }
}

void DearomatizationsGroups::_detectAromaticGroups (int v_idx, const int *atom_external_conn)
{
   int non_aromatic_conn = 0;
   if (atom_external_conn != 0)
      non_aromatic_conn = atom_external_conn[v_idx];

   const Vertex &vertex = _molecule.getVertex(v_idx);
   for (int i = vertex.neiBegin(); i != vertex.neiEnd(); i = vertex.neiNext(i)) 
   {
      int e_idx = vertex.neiEdge(i);
      int bond_order = _molecule.getBondOrder(e_idx);

      if (bond_order == -1)
         // Ignore such bonds.
         // It may be zero bonds from TautomerSuperStructure
         continue;
      if (bond_order != BOND_AROMATIC) 
      {
         non_aromatic_conn += bond_order;
         continue;
      }
      non_aromatic_conn++;

      int vn_idx = vertex.neiVertex(i);
      if (_vertexAromaticGroupIndex[vn_idx] != -1)
         continue;

      _vertexAromaticGroupIndex[vn_idx] = _vertexAromaticGroupIndex[v_idx];
      _detectAromaticGroups(vn_idx, atom_external_conn);
   }

   int label = _molecule.getAtomNumber(v_idx);
   int charge = _molecule.getAtomCharge(v_idx);
   int radical = _molecule.getAtomRadical(v_idx);

   int max_connectivity = Element::getMaximumConnectivity(label, 
      charge, radical, true);

   int atom_aromatic_connectivity = max_connectivity - non_aromatic_conn;
   if (atom_aromatic_connectivity < 0)
      throw Error("internal error: atom_aromatic_connectivity < 0");

   if (atom_aromatic_connectivity > 0)
      _vertexIsAcceptDoubleEdge[v_idx] = true;
   else
      _vertexIsAcceptDoubleEdge[v_idx] = false;
}

bool* DearomatizationsGroups::getAcceptDoubleBonds (void)
{
   return _vertexIsAcceptDoubleEdge.ptr();
}

bool DearomatizationsGroups::isAcceptDoubleBond (int atom)
{
   return _vertexIsAcceptDoubleEdge[atom];
}

//
// DearomatizationMatcher
//

DearomatizationMatcher::DearomatizationMatcher (DearomatizationsStorage &dearomatizations, 
   BaseMolecule &molecule, const int *atom_external_conn) 
   : 
   _molecule(molecule), _dearomatizations(dearomatizations),
   _graphMatchingFixedEdges(molecule), _aromaticGroups(molecule),
   TL_CP_GET(_matchedEdges),
   TL_CP_GET(_matchedEdgesState),
   TL_CP_GET(_groupExInfo),
   TL_CP_GET(_verticesInGroup),
   TL_CP_GET(_verticesAdded),
   TL_CP_GET(_edges2GroupMapping),
   TL_CP_GET(_edges2IndexInGroupMapping),
   TL_CP_GET(_correctEdgesArray),
   TL_CP_GET(_verticesFixCount),
   TL_CP_GET(_aromaticGroupsData)
{
   _needPrepare = true;
   _aromaticGroups.detectAromaticGroups(atom_external_conn);
}


bool DearomatizationMatcher::isAbleToFixBond (int edge_idx, int type)
{
   if (_dearomatizations.getDearomatizationParams() == Dearomatizer::PARAMS_NO_DEAROMATIZATIONS)
      return false;
   _prepare();

   int group = _edges2GroupMapping[edge_idx];
   if (group == -1)
      return false;

   if (type == BOND_TRIPLE)
      return false; // Triple bonds aren't supported

   _prepareGroup(group);
   if (_dearomatizations.getGroupDearomatizationsCount(group) == 0)
      return false;
   
   int offset = _groupExInfo[group].offsetInEdgesState;
   byte *groupFixedEdgesPtr      = _matchedEdges.ptr() + offset;

   int indexInGroup = _edges2IndexInGroupMapping[edge_idx];
   byte *groupFixedEdgesStatePtr = _matchedEdgesState.ptr() + offset;

   if (_dearomatizations.getDearomatizationParams() == Dearomatizer::PARAMS_SAVE_ALL_DEAROMATIZATIONS)
   {
      bitSetBit(groupFixedEdgesPtr, indexInGroup, 1);
      bitSetBit(groupFixedEdgesStatePtr, indexInGroup, type - 1);

      // Try to find dearomatization with the same edges in all dearomatizations
      int count = _dearomatizations.getGroupDearomatizationsCount(group);
      int activeEdgeState = _groupExInfo[group].activeEdgeState;
      int i;
      for (i = 0; i < count; i++)
      {
         const byte *dearomState = _dearomatizations.getGroupDearomatization(group, (i + activeEdgeState) % count);
         int nbits = _dearomatizations.getGroupBondsCount(group);
         if (bitTestEqualityByMask(dearomState, groupFixedEdgesStatePtr, groupFixedEdgesPtr, nbits))
         {
            _groupExInfo[group].activeEdgeState = i;
            break; // Dearomatization was found
         }
      }
      if (i != count)
      {
         _lastAcceptedEdge = edge_idx;
         _lastAcceptedEdgeType = type;
      }

      bitSetBit(groupFixedEdgesPtr, indexInGroup, 0);
      if (i != count)
         return true;
   }
   else
   {
      // Try to use active dearomatizations
      byte *activeDearom = _dearomatizations.getGroupDearomatization(group, 
         _groupExInfo[group].activeEdgeState);

      if (bitGetBit(activeDearom, indexInGroup) == type - 1)
      {
         bitSetBit(groupFixedEdgesStatePtr, indexInGroup, type - 1);
         _lastAcceptedEdge = edge_idx;
         _lastAcceptedEdgeType = type;
         return true;
      }

      // Try to modify current dearomatization
      _graphMatchingFixedEdges.setEdgesMappingPtr(_edges2IndexInGroupMapping.ptr());
      _graphMatchingFixedEdges.setMatchingEdgesPtr(activeDearom);
      _graphMatchingFixedEdges.setExtraInfo(groupFixedEdgesPtr);

      if (_fixBondInMatching(group, indexInGroup, type))
      {
         bitSetBit(groupFixedEdgesStatePtr, indexInGroup, type - 1);
         _lastAcceptedEdge = edge_idx;
         _lastAcceptedEdgeType = type;
         return true;
      }

      // Try to modify other dearomatizations
      bitSetBit(groupFixedEdgesPtr, indexInGroup, 1);
      bitSetBit(groupFixedEdgesStatePtr, indexInGroup, type - 1);

      int count = _dearomatizations.getGroupDearomatizationsCount(group);
      for (int i = 0; i < count - 1; i++)
      {
         int dearom_idx = (i + 1 + _groupExInfo[group].activeEdgeState) % count;
         // Get difference between current state and dearomatization state in group
         if (_tryToChangeActiveIndex(dearom_idx, group, groupFixedEdgesPtr, groupFixedEdgesStatePtr))
         {
            bitSetBit(groupFixedEdgesPtr, indexInGroup, 0);
            _groupExInfo[group].activeEdgeState = dearom_idx;
            _lastAcceptedEdge = edge_idx;
            _lastAcceptedEdgeType = type;
            return true;
         }
      }

      bitSetBit(groupFixedEdgesPtr, indexInGroup, 0);
      return false;
   }

   return false;
}

bool DearomatizationMatcher::fixBond (int edge_idx, int type)
{
   if (_dearomatizations.getDearomatizationParams() == Dearomatizer::PARAMS_NO_DEAROMATIZATIONS)
      return false;
   _prepare();

   int group = _edges2GroupMapping[edge_idx];
   if (group == -1)
      return false;

   if (_lastAcceptedEdge != edge_idx || _lastAcceptedEdgeType != type)
   {
      if (!isAbleToFixBond(edge_idx, type))
         return false;
      if (_lastAcceptedEdge != edge_idx || _lastAcceptedEdgeType != type)
         throw Error("DearomatizationMatcher::fixBond: internal error");
   }

   int offset = _groupExInfo[group].offsetInEdgesState;
   byte *groupFixedEdgesPtr      = _matchedEdges.ptr() + offset;
   byte *groupFixedEdgesStatePtr = _matchedEdgesState.ptr() + offset;

   int indexInGroup = _edges2IndexInGroupMapping[edge_idx];
   bitSetBit(groupFixedEdgesPtr, indexInGroup, 1);
   if (bitGetBit(groupFixedEdgesStatePtr, indexInGroup) != type - 1)
      throw Error("DearomatizationMatcher::fixBond: internal error #2");

   const Edge &edge = _molecule.getEdge(edge_idx);
   _verticesFixCount[edge.beg]++;
   _verticesFixCount[edge.end]++;

   _lastAcceptedEdge = -1;
   return true;
}

void DearomatizationMatcher::unfixBond (int edge_idx)
{
   if (_dearomatizations.getDearomatizationParams() == Dearomatizer::PARAMS_NO_DEAROMATIZATIONS)
      return;
   _prepare();

   int group = _edges2GroupMapping[edge_idx];
   if (group == -1)
      return;

   byte *groupFixedEdgesPtr = _matchedEdges.ptr() + _groupExInfo[group].offsetInEdgesState;
   bitSetBit(groupFixedEdgesPtr, _edges2IndexInGroupMapping[edge_idx], 0);

   const Edge &edge = _molecule.getEdge(edge_idx);
   _verticesFixCount[edge.beg]--;
   _verticesFixCount[edge.end]--;
}

void DearomatizationMatcher::unfixBondByAtom (int atom_idx)
{
   if (_dearomatizations.getDearomatizationParams() == Dearomatizer::PARAMS_NO_DEAROMATIZATIONS)
      return;
   _prepare();
   if (_verticesFixCount[atom_idx] == 0)
      return;

   const Vertex &vertex = _molecule.getVertex(atom_idx);
   for (int i = vertex.neiBegin(); i != vertex.neiEnd(); i = vertex.neiNext(i))
      unfixBond(vertex.neiEdge(i));
}

void DearomatizationMatcher::_prepare (void)
{
   if (!_needPrepare)
      return;

   if (_dearomatizations.getDearomatizationParams() == Dearomatizer::PARAMS_SAVE_JUST_HETERATOMS) 
   {
      _dearomatizations.clearBondsState();
      _aromaticGroups.constructGroups(_dearomatizations, true);
   }
   else
      _aromaticGroups.constructGroups(_dearomatizations, false);

   int offset = 0;
   _groupExInfo.resize(_dearomatizations.getGroupsCount());
   _edges2IndexInGroupMapping.resize(_molecule.edgeEnd());
   _edges2GroupMapping.resize(_molecule.edgeEnd());
   memset(_edges2IndexInGroupMapping.ptr(), -1, sizeof(int) * _edges2IndexInGroupMapping.size());
   memset(_edges2GroupMapping.ptr(), -1, sizeof(int) * _edges2GroupMapping.size());

   _verticesFixCount.resize(_molecule.vertexEnd());
   _verticesFixCount.zerofill();

   int maxGroupDearomatizations = 0;
   for (int group = 0; group < _dearomatizations.getGroupsCount(); group++)
   {
      _groupExInfo[group].offsetInEdgesState = offset;
      _groupExInfo[group].activeEdgeState = 0;

      if (_dearomatizations.getDearomatizationParams() == Dearomatizer::PARAMS_SAVE_JUST_HETERATOMS)
         _groupExInfo[group].needPrepare = true;
      else
         _groupExInfo[group].needPrepare = false;

      maxGroupDearomatizations = __max(maxGroupDearomatizations, 
         _dearomatizations.getGroupDearomatizationsCount(group));
      maxGroupDearomatizations = __max(maxGroupDearomatizations, 
         _dearomatizations.getGroupHeterAtomsStateCount(group));

      int edgesInGroup = _dearomatizations.getGroupBondsCount(group);
      const int *edges = _dearomatizations.getGroupBonds(group);
      for (int i = 0; i < edgesInGroup; i++)
      {
         int edge_idx = edges[i];
         _edges2GroupMapping[edge_idx] = group;
         _edges2IndexInGroupMapping[edge_idx] = i;
      }

      offset += bitGetSize(edgesInGroup);
   }

   _matchedEdges.resize(offset);
   _matchedEdges.zerofill();
   _matchedEdgesState.resize(_matchedEdges.size());
   _correctEdgesArray.resize(_matchedEdges.size());

   if (_dearomatizations.getDearomatizationParams() != Dearomatizer::PARAMS_SAVE_ALL_DEAROMATIZATIONS)
   {
      _verticesInGroup.reserve(_molecule.vertexEnd());
      _verticesAdded.resize(_molecule.vertexEnd());
      _verticesAdded.zeroFill();

      _generateUsedVertices();
      _graphMatchingFixedEdges.setAllVerticesInMatching();
   } 
   _lastAcceptedEdge = -1;
   _lastAcceptedEdgeType = -1;

   _needPrepare = false;
}

// Generate used vertices per each group
void DearomatizationMatcher::_generateUsedVertices()
{
   for (int group = 0; group < _dearomatizations.getGroupsCount(); group++)
   {
      _groupExInfo[group].offsetInVertices = _verticesInGroup.size();
      const int *groupBonds = _dearomatizations.getGroupBonds(group);
      int count = _dearomatizations.getGroupBondsCount(group);
      for (int i = 0; i < count; i++)
      {
         const Edge &edge = _molecule.getEdge(groupBonds[i]);
         if (!_verticesAdded.get(edge.beg))
         {
            _verticesInGroup.push(edge.beg);
            _verticesAdded.set(edge.beg);
         }
         if (!_verticesAdded.get(edge.end))
         {
            _verticesInGroup.push(edge.end);
            _verticesAdded.set(edge.end);
         }
      }
      _groupExInfo[group].verticesUsed = _verticesInGroup.size() - _groupExInfo[group].offsetInVertices;
   }
}

// Try to modify dearomatizations to have the same fixed bonds
bool DearomatizationMatcher::_tryToChangeActiveIndex (int dearom_idx, int group, 
                                                      byte *groupFixedEdgesPtr, byte *groupFixedEdgesStatePtr)
{
   int bondsCount = _dearomatizations.getGroupBondsCount(group);
   byte *dearomState = _dearomatizations.getGroupDearomatization(group, dearom_idx);

   bitGetAandBxorNotC(groupFixedEdgesPtr, groupFixedEdgesStatePtr, 
      dearomState, _correctEdgesArray.ptr(), bondsCount);
   _graphMatchingFixedEdges.setExtraInfo(_correctEdgesArray.ptr());
   _graphMatchingFixedEdges.setMatchingEdgesPtr(dearomState);

   int bytesCount = bitGetSize(bondsCount);
   for (int i = 0; i < bytesCount; i++)
   {
      byte dif = groupFixedEdgesPtr[i] & (groupFixedEdgesStatePtr[i] ^ dearomState[i]);
      while (dif != 0)
      {
         int indexInGroup = bitGetOneLOIndex(dif) + i * 8;
         if (indexInGroup > bondsCount)
            return true;

         if (!_fixBondInMatching(group, indexInGroup, bitGetBit(groupFixedEdgesStatePtr, indexInGroup) + 1))
            return false;

         // Update correct edges
         _correctEdgesArray[i] = groupFixedEdgesPtr[i] & (groupFixedEdgesStatePtr[i] ^ ~dearomState[i]);
         dif = groupFixedEdgesPtr[i] & (groupFixedEdgesStatePtr[i] ^ dearomState[i]);
      }
   }

   return true;
}

bool DearomatizationMatcher::_fixBondInMatching (int group, int indexInGroup, int type)
{
   const int *aromEdges = _dearomatizations.getGroupBonds(group);
   const Edge &edge = _molecule.getEdge(aromEdges[indexInGroup]);
   bool found = _graphMatchingFixedEdges.findAlternatingPath(edge.beg, edge.end, 
      type != BOND_SINGLE, type != BOND_SINGLE);
   if (found)
   {
      if (type == BOND_SINGLE)
      {
         _graphMatchingFixedEdges.setEdgeMatching(aromEdges[indexInGroup], false);
         _graphMatchingFixedEdges.processPath();
      }
      else 
      {
         _graphMatchingFixedEdges.processPath();
         _graphMatchingFixedEdges.setEdgeMatching(aromEdges[indexInGroup], true);
      }
      return true;
   }
   return false;
}

void DearomatizationMatcher::_prepareGroup (int group)
{
   if (!_groupExInfo[group].needPrepare)
      return;

   _groupExInfo[group].needPrepare = false;
   if (_dearomatizations.getGroupHeteroAtomsCount(group) != 0 && 
         _dearomatizations.getGroupHeterAtomsStateCount(group) == 0)
      return;
   // Create mapping from local hetero-atoms indices to atom indices in molecule
   _aromaticGroups.getGroupDataFromStorage(_dearomatizations, group, &_aromaticGroupsData);

   GraphMatchingVerticesFixed graphMatchingFixedVertices(_molecule);

   graphMatchingFixedVertices.setEdgesMappingPtr(_aromaticGroupsData.bondsInvMapping.ptr());
   graphMatchingFixedVertices.setVerticesSetPtr(_aromaticGroupsData.vertices.ptr(),
      _aromaticGroupsData.vertices.size());

   graphMatchingFixedVertices.setVerticesMapping(_aromaticGroupsData.heteroAtomsInvMapping.ptr());
   graphMatchingFixedVertices.setVerticesAccept(_aromaticGroups.getAcceptDoubleBonds());

   // Generate one dearomatization for each hetero-atoms configuration
   int count = _dearomatizations.getGroupHeterAtomsStateCount(group);
   int index = 0;
   do 
   {
      if (count != 0)
      {
         const byte *heteroAtomsState = _dearomatizations.getGroupHeterAtomsState(group, index++);
         graphMatchingFixedVertices.setVerticesState(heteroAtomsState);
      }
      if (!graphMatchingFixedVertices.findMatching())
         throw Error("DearomatizationMatcher::_prepareGroup: internal error");

      _dearomatizations.addGroupDearomatization(group, graphMatchingFixedVertices.getEdgesState());

      graphMatchingFixedVertices.reset();
   } while(index < count);
}


//
// DearomatizationMatcher::GraphMatchingEdgeFixed
//

void DearomatizationMatcher::GraphMatchingEdgeFixed::setExtraInfo (byte *edgesEdges)
{
   _edgesState = edgesEdges;
}

bool DearomatizationMatcher::GraphMatchingEdgeFixed::checkEdge (int e_idx)
{
   return !bitGetBit(_edgesState, _edgesMapping[e_idx]);
}

DearomatizationMatcher::GraphMatchingEdgeFixed::GraphMatchingEdgeFixed 
   (BaseMolecule &molecule) : GraphPerfectMatching(molecule, USE_EXTERNAL_EDGES_PTR |
      USE_EDGES_MAPPING | USE_VERTICES_SET)
{
   _edgesState = NULL;
}

//
// DearomatizationMatcher::GraphMatchingVerticesFixed
//

bool DearomatizationMatcher::GraphMatchingVerticesFixed::checkVertex (int v_idx)
{
   if (_verticesMapping[v_idx] != -1)
      return bitGetBit(_verticesState, _verticesMapping[v_idx]) == 1;
   return _verticesAcceptDoubleBond[v_idx];
}

void DearomatizationMatcher::GraphMatchingVerticesFixed::setVerticesState (const byte *verticesState)
{
   _verticesState = verticesState;
}

void DearomatizationMatcher::GraphMatchingVerticesFixed::setVerticesMapping (int *verticesMapping)
{
   _verticesMapping = verticesMapping;
}

void DearomatizationMatcher::GraphMatchingVerticesFixed::setVerticesAccept (bool *verticesAcceptDoubleBond)
{
   _verticesAcceptDoubleBond = verticesAcceptDoubleBond;
}

DearomatizationMatcher::GraphMatchingVerticesFixed::GraphMatchingVerticesFixed
   (BaseMolecule &molecule) :
   GraphPerfectMatching(molecule, USE_EDGES_MAPPING | USE_VERTICES_SET)
{
   _verticesState = NULL;
   _verticesMapping = NULL;
   _verticesAcceptDoubleBond = NULL;
}

//
// MoleculeDearomatizer
//
MoleculeDearomatizer::MoleculeDearomatizer (Molecule &mol, DearomatizationsStorage &dearom) :
   _dearomatizations(dearom), _mol(mol)
{
}

void MoleculeDearomatizer::dearomatizeGroup (int group, int dearomatization_index)
{
   byte *bondsState = _dearomatizations.getGroupDearomatization(group, dearomatization_index);
   const int *bondsMap = _dearomatizations.getGroupBonds(group);
   int bondsCount = _dearomatizations.getGroupBondsCount(group);

   for (int i = 0; i < bondsCount; i++)
   {
      if (bitGetBit(bondsState, i))
         _mol.setBondOrder(bondsMap[i], BOND_DOUBLE);
      else
         _mol.setBondOrder(bondsMap[i], BOND_SINGLE);
   }
}

bool MoleculeDearomatizer::dearomatizeMolecule (Molecule &mol)
{
   DearomatizationsStorage dst;
   Dearomatizer dearomatizer(mol, 0);
   dearomatizer.setDearomatizationParams(Dearomatizer::PARAMS_SAVE_ONE_DEAROMATIZATION);
   dearomatizer.enumerateDearomatizations(dst);
   MoleculeDearomatizer mol_dearom(mol, dst);

   bool all_dearomatzied = true;
   for (int i = 0; i < dst.getGroupsCount(); ++i)
      if (dst.getGroupDearomatizationsCount(i) != 0)
         mol_dearom.dearomatizeGroup(i, 0);
      else
         all_dearomatzied = false;
   return all_dearomatzied;
}
