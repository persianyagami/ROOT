// @(#)root/minuit2:$Id$
// Authors: M. Winkler, F. James, L. Moneta, A. Zsenei   2003-2005  

/**********************************************************************
 *                                                                    *
 * Copyright (c) 2005 LCG ROOT Math team,  CERN/PH-SFT                *
 *                                                                    *
 **********************************************************************/

#include "Minuit2/MnHesse.h"
#include "Minuit2/MnUserParameterState.h"
#include "Minuit2/MnUserFcn.h"
#include "Minuit2/FCNBase.h"
#include "Minuit2/MnPosDef.h"
#include "Minuit2/HessianGradientCalculator.h"
#include "Minuit2/Numerical2PGradientCalculator.h"
#include "Minuit2/InitialGradientCalculator.h"
#include "Minuit2/MinimumState.h"
#include "Minuit2/VariableMetricEDMEstimator.h"
#include "Minuit2/FunctionMinimum.h"
#include "TMatrixDSym.h"

//#define DEBUG

#if defined(DEBUG) || defined(WARNINGMSG)
#include "Minuit2/MnPrint.h"
#endif

#include "Minuit2/MPIProcess.h"

namespace ROOT {

   namespace Minuit2 {

TMatrixDSym* MnHesse::_hessian = 0 ;

void MnHesse::clearLastHessian() {
  if (_hessian) {
    delete _hessian ;
    _hessian = 0 ;
  }
}

MnUserParameterState MnHesse::operator()(const FCNBase& fcn, const std::vector<double>& par, const std::vector<double>& err, unsigned int maxcalls) const { 
   // interface from vector of params and errors
   return (*this)(fcn, MnUserParameterState(par, err), maxcalls);
}

MnUserParameterState MnHesse::operator()(const FCNBase& fcn, const std::vector<double>& par, unsigned int nrow, const std::vector<double>& cov,  unsigned int maxcalls) const {
   // interface from vector of params and covariance
   return (*this)(fcn, MnUserParameterState(par, cov, nrow), maxcalls);
}

MnUserParameterState MnHesse::operator()(const FCNBase& fcn, const std::vector<double>& par, const MnUserCovariance& cov, unsigned int maxcalls) const {
    // interface from vector of params and covariance
   return (*this)(fcn, MnUserParameterState(par, cov), maxcalls);
}

MnUserParameterState MnHesse::operator()(const FCNBase& fcn, const MnUserParameters& par, unsigned int maxcalls) const {
   // interface from MnUserParameters
   return (*this)(fcn, MnUserParameterState(par), maxcalls);
}

MnUserParameterState MnHesse::operator()(const FCNBase& fcn, const MnUserParameters& par, const MnUserCovariance& cov, unsigned int maxcalls) const {
   // interface from MnUserParameters and MnUserCovariance
   return (*this)(fcn, MnUserParameterState(par, cov), maxcalls);
}

MnUserParameterState MnHesse::operator()(const FCNBase& fcn, const MnUserParameterState& state, unsigned int maxcalls) const {
   // interface from MnUserParameterState 
   // create a new Minimum state and use that interface
   unsigned int n = state.VariableParameters();
   MnUserFcn mfcn(fcn, state.Trafo(),state.NFcn());
   MnAlgebraicVector x(n);
   for(unsigned int i = 0; i < n; i++) x(i) = state.IntParameters()[i];
   double amin = mfcn(x);
   Numerical2PGradientCalculator gc(mfcn, state.Trafo(), fStrategy);
   MinimumParameters par(x, amin);
   FunctionGradient gra = gc(par);
   MinimumState tmp = (*this)(mfcn, MinimumState(par, MinimumError(MnAlgebraicSymMatrix(n), 1.), gra, state.Edm(), state.NFcn()), state.Trafo(), maxcalls);
   
   return MnUserParameterState(tmp, fcn.Up(), state.Trafo());
}

void MnHesse::operator()(const FCNBase& fcn, FunctionMinimum& min, unsigned int maxcalls) const {
   // interface from FunctionMinimum to be used after minimization 
   // use last state from the minimization without the need to re-create a new state
   // do not reset function calls and keep updating them
   MnUserFcn mfcn(fcn, min.UserState().Trafo(),min.NFcn());
   MinimumState st = (*this)( mfcn, min.State(), min.UserState().Trafo(), maxcalls); 
   min.Add(st); 
}

MinimumState MnHesse::operator()(const MnFcn& mfcn, const MinimumState& st, const MnUserTransformation& trafo, unsigned int maxcalls) const {
   // internal interface from MinimumState and MnUserTransformation
   // Function who does the real Hessian calculations
   
   const MnMachinePrecision& prec = trafo.Precision();
   // make sure starting at the right place
   double amin = mfcn(st.Vec());
   double aimsag = sqrt(prec.Eps2())*(fabs(amin)+mfcn.Up());
   
   // diagonal Elements first
   
   unsigned int n = st.Parameters().Vec().size();
   if(maxcalls == 0) maxcalls = 200 + 100*n + 5*n*n;
   
   MnAlgebraicSymMatrix vhmat(n);
   MnAlgebraicVector g2 = st.Gradient().G2();
   MnAlgebraicVector gst = st.Gradient().Gstep();
   MnAlgebraicVector grd = st.Gradient().Grad();
   MnAlgebraicVector dirin = st.Gradient().Gstep();
   MnAlgebraicVector yy(n);


   // case gradient is not numeric (could be analytical or from FumiliGradientCalculator)

   if(st.Gradient().IsAnalytical()  ) {
      Numerical2PGradientCalculator igc(mfcn, trafo, fStrategy);
      FunctionGradient tmp = igc(st.Parameters());
      gst = tmp.Gstep();
      dirin = tmp.Gstep();
      g2 = tmp.G2();
   }
   
   MnAlgebraicVector x = st.Parameters().Vec(); 

#ifdef DEBUG
   std::cout << "\nMnHesse " << std::endl;
   std::cout << " x " << x << std::endl;
   std::cout << " amin " << amin << "  " << st.Fval() << std::endl;
   std::cout << " grd " << grd << std::endl;
   std::cout << " gst " << gst << std::endl;
   std::cout << " g2  " << g2 << std::endl;
   std::cout << " Gradient is analytical  " << st.Gradient().IsAnalytical() << std::endl;
#endif

   
   for(unsigned int i = 0; i < n; i++) {
      
      double xtf = x(i);
      double dmin = 8.*prec.Eps2()*(fabs(xtf) + prec.Eps2());
      double d = fabs(gst(i));
      if(d < dmin) d = dmin;

#ifdef DEBUG
      std::cout << "\nDerivative parameter  " << i << " d = " << d << " dmin = " << dmin << std::endl;
#endif

      
      for(unsigned int icyc = 0; icyc < Ncycles(); icyc++) {
         double sag = 0.;
         double fs1 = 0.;
         double fs2 = 0.;
         for(unsigned int multpy = 0; multpy < 5; multpy++) {
            x(i) = xtf + d;
            fs1 = mfcn(x);
            x(i) = xtf - d;
            fs2 = mfcn(x);
            x(i) = xtf;
            sag = 0.5*(fs1+fs2-2.*amin);

#ifdef DEBUG
            std::cout << "cycle " << icyc << " mul " << multpy << "\t sag = " << sag << " d = " << d << std::endl; 
#endif
            //  Now as F77 Minuit - check taht sag is not zero
            if (sag != 0) goto L30; // break
            if(trafo.Parameter(i).HasLimits()) {
               if(d > 0.5) goto L26;
               d *= 10.;
               if(d > 0.5) d = 0.51;
               continue;
            }
            d *= 10.;
         }
         
L26:  
#ifdef WARNINGMSG

         // get parameter name for i
         // (need separate scope for avoiding compl error when declaring name)
         {  
            const char * name = trafo.Name( trafo.ExtOfInt(i));
            MN_INFO_VAL2("MnHesse: 2nd derivative zero for Parameter ", name);
            MN_INFO_MSG("MnHesse fails and will return diagonal matrix ");
         }
#endif
         
         for(unsigned int j = 0; j < n; j++) {
            double tmp = g2(j) < prec.Eps2() ? 1. : 1./g2(j);
            vhmat(j,j) = tmp < prec.Eps2() ? 1. : tmp;
         }
         
         return MinimumState(st.Parameters(), MinimumError(vhmat, MinimumError::MnHesseFailed()), st.Gradient(), st.Edm(), mfcn.NumOfCalls());
         
L30:      
            double g2bfor = g2(i);
         g2(i) = 2.*sag/(d*d);
         grd(i) = (fs1-fs2)/(2.*d);
         gst(i) = d;
         dirin(i) = d;
         yy(i) = fs1;
         double dlast = d;
         d = sqrt(2.*aimsag/fabs(g2(i)));
         if(trafo.Parameter(i).HasLimits()) d = std::min(0.5, d);
         if(d < dmin) d = dmin;

#ifdef DEBUG
         std::cout << "\t g1 = " << grd(i) << " g2 = " << g2(i) << " step = " << gst(i) << " d = " << d 
                   << " diffd = " <<  fabs(d-dlast)/d << " diffg2 = " << fabs(g2(i)-g2bfor)/g2(i) << std::endl;
#endif

         
         // see if converged
         if(fabs((d-dlast)/d) < Tolerstp()) break;
         if(fabs((g2(i)-g2bfor)/g2(i)) < TolerG2()) break; 
         d = std::min(d, 10.*dlast);
         d = std::max(d, 0.1*dlast);   
      }
      vhmat(i,i) = g2(i);
      if(mfcn.NumOfCalls()  > maxcalls) {
         
#ifdef WARNINGMSG
         //std::cout<<"maxcalls " << maxcalls << " " << mfcn.NumOfCalls() << "  " <<   st.NFcn() << std::endl;
         MN_INFO_MSG("MnHesse: maximum number of allowed function calls exhausted.");  
         MN_INFO_MSG("MnHesse fails and will return diagonal matrix ");
#endif
         
         for(unsigned int j = 0; j < n; j++) {
            double tmp = g2(j) < prec.Eps2() ? 1. : 1./g2(j);
            vhmat(j,j) = tmp < prec.Eps2() ? 1. : tmp;
         }
         
         return MinimumState(st.Parameters(), MinimumError(vhmat, MinimumError::MnHesseFailed()), st.Gradient(), st.Edm(), mfcn.NumOfCalls());
      }
      
   }

#ifdef DEBUG
   std::cout << "\n Second derivatives " << g2 << std::endl; 
#endif   
   
   if(fStrategy.Strategy() > 0) {
      // refine first derivative
      HessianGradientCalculator hgc(mfcn, trafo, fStrategy);
      FunctionGradient gr = hgc(st.Parameters(), FunctionGradient(grd, g2, gst));
      // update gradient and step values 
      grd = gr.Grad();
      gst = gr.Gstep();
   }
   
   //off-diagonal Elements  
   // initial starting values
   MPIProcess mpiprocOffDiagonal(n*(n-1)/2,0);
   unsigned int startParIndexOffDiagonal = mpiprocOffDiagonal.StartElementIndex();
   unsigned int endParIndexOffDiagonal = mpiprocOffDiagonal.EndElementIndex();

   unsigned int offsetVect = 0;
   for (unsigned int in = 0; in<startParIndexOffDiagonal; in++)
      if ((in+offsetVect)%(n-1)==0) offsetVect += (in+offsetVect)/(n-1);

   for (unsigned int in = startParIndexOffDiagonal;
        in<endParIndexOffDiagonal; in++) {

      int i = (in+offsetVect)/(n-1);
      if ((in+offsetVect)%(n-1)==0) offsetVect += i;
      int j = (in+offsetVect)%(n-1)+1;

      if ((i+1)==j || in==startParIndexOffDiagonal)
         x(i) += dirin(i);
      
      x(j) += dirin(j);
      
      double fs1 = mfcn(x);
      double elem = (fs1 + amin - yy(i) - yy(j))/(dirin(i)*dirin(j));
      vhmat(i,j) = elem;
      
      x(j) -= dirin(j);
      
      if (j%(n-1)==0 || in==endParIndexOffDiagonal-1)
         x(i) -= dirin(i);
      
   }
   
   mpiprocOffDiagonal.SyncSymMatrixOffDiagonal(vhmat);

   //verify if matrix pos-def (still 2nd derivative)

#ifdef DEBUG
   std::cout << "Original error matrix " << vhmat << std::endl;
#endif

   // wve save Hessian here for later use
   if (_hessian) delete _hessian ;
   _hessian = new TMatrixDSym(vhmat.Nrow()) ;
   for (unsigned int i=0 ; i<vhmat.Nrow() ; i++) {
     for (unsigned int j=0 ; j<vhmat.Nrow() ; j++) {
       (*_hessian)(i,j) = vhmat(i,j) ;
     }
   }

   MinimumError tmpErr = MnPosDef()(MinimumError(vhmat,1.), prec);

#ifdef DEBUG
   std::cout << "Original error matrix " << vhmat << std::endl;
#endif

   vhmat = tmpErr.InvHessian();

#ifdef DEBUG
   std::cout << "PosDef error matrix " << vhmat << std::endl;
#endif


   int ifail = Invert(vhmat);
   if(ifail != 0) {
      
#ifdef WARNINGMSG
      MN_INFO_MSG("MnHesse: matrix inversion fails!");
      MN_INFO_MSG("MnHesse fails and will return diagonal matrix.");
#endif
      
      MnAlgebraicSymMatrix tmpsym(vhmat.Nrow());
      for(unsigned int j = 0; j < n; j++) {
         double tmp = g2(j) < prec.Eps2() ? 1. : 1./g2(j);
         tmpsym(j,j) = tmp < prec.Eps2() ? 1. : tmp;
      }
      
      return MinimumState(st.Parameters(), MinimumError(tmpsym, MinimumError::MnInvertFailed()), st.Gradient(), st.Edm(), mfcn.NumOfCalls());
   }
   
   FunctionGradient gr(grd, g2, gst);
   VariableMetricEDMEstimator estim;
   
   // if matrix is made pos def returns anyway edm
   if(tmpErr.IsMadePosDef()) {
      MinimumError err(vhmat, MinimumError::MnMadePosDef() );
      double edm = estim.Estimate(gr, err);
#ifdef WARNINGMSG
      MN_INFO_MSG("MnHesse: matrix was forced pos. def. ");
#endif
      return MinimumState(st.Parameters(), err, gr, edm, mfcn.NumOfCalls());
   }
   
   //calculate edm for good errors
   MinimumError err(vhmat, 0.);
   double edm = estim.Estimate(gr, err);

#ifdef DEBUG
   std::cout << "\nNew state from MnHesse " << std::endl;
   std::cout << "Gradient " << grd << std::endl; 
   std::cout << "Second Deriv " << g2 << std::endl; 
   std::cout << "Gradient step " << gst << std::endl; 
   std::cout << "Error  " << vhmat  << std::endl; 
   std::cout << "edm  " << edm  << std::endl; 
#endif

   
   return MinimumState(st.Parameters(), err, gr, edm, mfcn.NumOfCalls());
}

/*
 MinimumError MnHesse::Hessian(const MnFcn& mfcn, const MinimumState& st, const MnUserTransformation& trafo) const {
    
    const MnMachinePrecision& prec = trafo.Precision();
    // make sure starting at the right place
    double amin = mfcn(st.Vec());
    //   if(fabs(amin - st.Fval()) > prec.Eps2()) std::cout<<"function Value differs from amin  by "<<amin - st.Fval()<<std::endl;
    
    double aimsag = sqrt(prec.Eps2())*(fabs(amin)+mfcn.Up());
    
    // diagonal Elements first
    
    unsigned int n = st.Parameters().Vec().size();
    MnAlgebraicSymMatrix vhmat(n);
    MnAlgebraicVector g2 = st.Gradient().G2();
    MnAlgebraicVector gst = st.Gradient().Gstep();
    MnAlgebraicVector grd = st.Gradient().Grad();
    MnAlgebraicVector dirin = st.Gradient().Gstep();
    MnAlgebraicVector yy(n);
    MnAlgebraicVector x = st.Parameters().Vec(); 
    
    for(unsigned int i = 0; i < n; i++) {
       
       double xtf = x(i);
       double dmin = 8.*prec.Eps2()*fabs(xtf);
       double d = fabs(gst(i));
       if(d < dmin) d = dmin;
       for(int icyc = 0; icyc < Ncycles(); icyc++) {
          double sag = 0.;
          double fs1 = 0.;
          double fs2 = 0.;
          for(int multpy = 0; multpy < 5; multpy++) {
             x(i) = xtf + d;
             fs1 = mfcn(x);
             x(i) = xtf - d;
             fs2 = mfcn(x);
             x(i) = xtf;
             sag = 0.5*(fs1+fs2-2.*amin);
             if(sag > prec.Eps2()) break;
             if(trafo.Parameter(i).HasLimits()) {
                if(d > 0.5) {
                   std::cout<<"second derivative zero for Parameter "<<i<<std::endl;
                   std::cout<<"return diagonal matrix "<<std::endl;
                   for(unsigned int j = 0; j < n; j++) {
                      vhmat(j,j) = (g2(j) < prec.Eps2() ? 1. : 1./g2(j));
                      return MinimumError(vhmat, 1., false);
                   }
                }
                d *= 10.;
                if(d > 0.5) d = 0.51;
                continue;
             }
             d *= 10.;
          }
          if(sag < prec.Eps2()) {
             std::cout<<"MnHesse: internal loop exhausted, return diagonal matrix."<<std::endl;
             for(unsigned int i = 0; i < n; i++)
                vhmat(i,i) = (g2(i) < prec.Eps2() ? 1. : 1./g2(i));
             return MinimumError(vhmat, 1., false);
          }
          double g2bfor = g2(i);
          g2(i) = 2.*sag/(d*d);
          grd(i) = (fs1-fs2)/(2.*d);
          gst(i) = d;
          dirin(i) = d;
          yy(i) = fs1;
          double dlast = d;
          d = sqrt(2.*aimsag/fabs(g2(i)));
          if(trafo.Parameter(i).HasLimits()) d = std::min(0.5, d);
          if(d < dmin) d = dmin;
          
          // see if converged
          if(fabs((d-dlast)/d) < Tolerstp()) break;
          if(fabs((g2(i)-g2bfor)/g2(i)) < TolerG2()) break; 
          d = std::min(d, 10.*dlast);
          d = std::max(d, 0.1*dlast);
       }
       vhmat(i,i) = g2(i);
    }
    
    //off-diagonal Elements  
    for(unsigned int i = 0; i < n; i++) {
       x(i) += dirin(i);
       for(unsigned int j = i+1; j < n; j++) {
          x(j) += dirin(j);
          double fs1 = mfcn(x);
          double elem = (fs1 + amin - yy(i) - yy(j))/(dirin(i)*dirin(j));
          vhmat(i,j) = elem;
          x(j) -= dirin(j);
       }
       x(i) -= dirin(i);
    }
    
    return MinimumError(vhmat, 0.);
 }
 */

  }  // namespace Minuit2

}  // namespace ROOT
