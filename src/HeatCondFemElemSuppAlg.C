/*------------------------------------------------------------------------*/
/*  Copyright 2014 Sandia Corporation.                                    */
/*  This software is released under the license detailed                  */
/*  in the file, LICENSE, which is located in the top-level Nalu          */
/*  directory structure                                                   */
/*------------------------------------------------------------------------*/


#include <HeatCondFemElemSuppAlg.h>
#include <SupplementalAlgorithm.h>
#include <FieldTypeDef.h>
#include <Realm.h>
#include <master_element/Hex8FEM.h>

// stk_mesh/base/fem
#include <stk_mesh/base/Entity.hpp>
#include <stk_mesh/base/MetaData.hpp>
#include <stk_mesh/base/BulkData.hpp>
#include <stk_mesh/base/Field.hpp>

namespace sierra{
namespace nalu{

//==========================================================================
// Class Definition
//==========================================================================
// HeatCondFemElemSuppAlg - hack hex8 FEM heat conduction
//==========================================================================
//--------------------------------------------------------------------------
//-------- constructor -----------------------------------------------------
//--------------------------------------------------------------------------
HeatCondFemElemSuppAlg::HeatCondFemElemSuppAlg(
  Realm &realm)
: SupplementalAlgorithm(realm),
    bulkData_(&realm.bulk_data()),
    temperature_(NULL),
    thermalCond_(NULL),
    coordinates_(NULL),
    meFEM_(new Hex8FEM()),
    ipWeight_(&meFEM_->weights_[0]),
    nDim_(realm.meta_data().spatial_dimension())
{
  // save off fields
  stk::mesh::MetaData & meta_data = realm_.meta_data();
  temperature_ = meta_data.get_field<ScalarFieldType>(stk::topology::NODE_RANK, "temperature");
  thermalCond_ = meta_data.get_field<ScalarFieldType>(stk::topology::NODE_RANK, "thermal_conductivity");
  coordinates_ = meta_data.get_field<VectorFieldType>(stk::topology::NODE_RANK, "coordinates");
}

//--------------------------------------------------------------------------
//-------- elem_resize -----------------------------------------------------
//--------------------------------------------------------------------------
void
HeatCondFemElemSuppAlg::elem_resize(
  MasterElement */*meFEM_*/,
  MasterElement */*meSCV*/)
{
  const int nodesPerElement = meFEM_->nodesPerElement_;
  const int numIp = meFEM_->numIntPoints_;

  // resize; geometry
  ws_dndx_.resize(nDim_*numIp*nodesPerElement);
  ws_deriv_.resize(nDim_*numIp*nodesPerElement);
  ws_det_j_.resize(numIp);
  ws_shape_function_.resize(numIp*nodesPerElement);

  // resize; fields
  ws_temperature_.resize(nodesPerElement);
  ws_thermalCond_.resize(nodesPerElement);
  ws_coordinates_.resize(nDim_*nodesPerElement);
  
  // compute shape function
  meFEM_->shape_fcn(&ws_shape_function_[0]);
}

//--------------------------------------------------------------------------
//-------- setup -----------------------------------------------------------
//--------------------------------------------------------------------------
void
HeatCondFemElemSuppAlg::setup()
{
  // nothing to do
}

//--------------------------------------------------------------------------
//-------- elem_execute ----------------------------------------------------
//--------------------------------------------------------------------------
void
HeatCondFemElemSuppAlg::elem_execute(
  double *lhs,
  double *rhs,
  stk::mesh::Entity element,
  MasterElement */*meSCS_*/,
  MasterElement */*meSCV*/)
{
  // details on this element topo
  const int nodesPerElement = meFEM_->nodesPerElement_;
  const int numIp = meFEM_->numIntPoints_;
  
  // gather
  stk::mesh::Entity const *  node_rels = bulkData_->begin_nodes(element);
  int num_nodes = bulkData_->num_nodes(element);

  // sanity check on num nodes
  ThrowAssert( num_nodes == nodesPerElement );
  for ( int ni = 0; ni < num_nodes; ++ni ) {
    stk::mesh::Entity node = node_rels[ni];
    
    // gather scalars
    ws_temperature_[ni] = *stk::mesh::field_data(*temperature_, node);
    ws_thermalCond_[ni] = *stk::mesh::field_data(*thermalCond_, node );

    // pointers to real data
    const double * coords = stk::mesh::field_data(*coordinates_, node );

    // gather vectors
    const int offSet = ni*nDim_;
    for ( int j=0; j < nDim_; ++j ) {
      ws_coordinates_[offSet+j] = coords[j];
    }
  }

  // compute dndx; det_j comes for free
  double me_error = 0.0;
  meFEM_->grad_op(1, &ws_coordinates_[0], &ws_dndx_[0], &ws_deriv_[0], &ws_det_j_[0], &me_error);

  for ( int ip = 0; ip < numIp; ++ip ) {

    // compute ip property
    double thermalCondIp = 0.0;
    const int ipNpe = ip*nodesPerElement;
    for ( int ic = 0; ic < nodesPerElement; ++ic ) {
      const double r = ws_shape_function_[ipNpe+ic];
      thermalCondIp += r*ws_thermalCond_[ic];
    }

    // start the assembly
    const double ipFactor = ws_det_j_[ip]*ipWeight_[ip];

    // row ir
    for ( int ir = 0; ir < nodesPerElement; ++ir) {

      // offset for row dndx
      const int offSetDnDxIr = nDim_*nodesPerElement*ip + ir*nDim_;

      // column ic
      double rhsSum = 0.0;
      for ( int ic = 0; ic < nodesPerElement; ++ic ) {
        // offset for column dndx
        const int offSetDnDxIc = nDim_*nodesPerElement*ip + ic*nDim_;

        double lhsSum = 0.0;
        double T = ws_temperature_[ic];
        for ( int j = 0; j < nDim_; ++j ) {
          const double fac = ws_dndx_[offSetDnDxIr+j]*thermalCondIp*ws_dndx_[offSetDnDxIc+j];
          lhsSum += fac;
          rhsSum += fac*T;
        }
        lhs[ir*nodesPerElement+ic] += lhsSum*ipFactor;
      }
      rhs[ir] -= rhsSum*ipFactor;
    }
  }
}
  
} // namespace nalu
} // namespace Sierra
