#include "NewtonEuler.h"
#include <math/random.h>
#include <math/CholeskyDecomposition.h>
using namespace std;


void RigidBodyVelocity::setTransformed(const RigidBodyVelocity& vel,const RigidTransform& T)
{
  v = T.R*(vel.v + cross(vel.w,T.t));
  w = T.R*vel.w;
}

void RigidBodyVelocity::setShifted(const RigidBodyVelocity& vel,const Vector3& shift)
{
  v = vel.v + cross(vel.w,shift);
  w = vel.w;
}

void Wrench::setForceAtPoint(const Vector3& _f,const Vector3& _p)
{
  f = _f;
  m.setCross(_p,_f);
}

void Wrench::setTransformed(const Wrench& wr,const RigidTransform& T)
{
  f = T.R*wr.f;
  m = T.R*(cross(T.t,wr.f)+wr.m);
}

void Wrench::setShifted(const Wrench& wr,const Vector3& shift)
{
  f = wr.f;
  m = cross(shift,wr.f)+wr.m;
}



SpatialVector::SpatialVector()
  :Vector(6,Zero)
{}

void SpatialVector::set(const Vector3& a,const Vector3& b)
{
  a.get((*this)(0),(*this)(1),(*this)(2));
  b.get((*this)(3),(*this)(4),(*this)(5));
}

void SpatialVector::get(Vector3& a,Vector3& b) const
{
  a.set((*this)(0),(*this)(1),(*this)(2));
  b.set((*this)(3),(*this)(4),(*this)(5));
}

SpatialMatrix::SpatialMatrix()
  :Matrix(6,6,Zero)
{}

void SpatialMatrix::setUpperLeft(const Matrix3& mat11)
{
  for(int i=0;i<3;i++)
    for(int j=0;j<3;j++)
      (*this)(i,j) = mat11(i,j);
}

void SpatialMatrix::setLowerRight(const Matrix3& mat22)
{
  for(int i=0;i<3;i++)
    for(int j=0;j<3;j++)
      (*this)(i+3,j+3) = mat22(i,j);
}

void SpatialMatrix::setUpperRight(const Matrix3& mat12)
{
  for(int i=0;i<3;i++)
    for(int j=0;j<3;j++)
      (*this)(i,j+3) = mat12(i,j);
}

void SpatialMatrix::setLowerLeft(const Matrix3& mat21)
{
  for(int i=0;i<3;i++)
    for(int j=0;j<3;j++)
      (*this)(i+3,j) = mat21(i,j);
}

void SpatialMatrix::getUpperLeft(Matrix3& mat11) const
{
  for(int i=0;i<3;i++)
    for(int j=0;j<3;j++)
      mat11(i,j) = (*this)(i,j);
}

void SpatialMatrix::getLowerRight(Matrix3& mat22) const
{
  for(int i=0;i<3;i++)
    for(int j=0;j<3;j++)
      mat22(i,j) = (*this)(i+3,j+3);
}

void SpatialMatrix::getUpperRight(Matrix3& mat12) const
{
  for(int i=0;i<3;i++)
    for(int j=0;j<3;j++)
      mat12(i,j) = (*this)(i,j+3);
}

void SpatialMatrix::getLowerLeft(Matrix3& mat21) const
{
  for(int i=0;i<3;i++)
    for(int j=0;j<3;j++)
      mat21(i,j) = (*this)(i+3,j);
}

void SpatialMatrix::setForceShift(const Vector3& origMomentCenter,const Vector3& newMomentCenter)
{
  setZero();
  setIdentity();
  Matrix3 cp;
  cp.setCrossProduct(newMomentCenter-origMomentCenter);
  cp.inplaceTranspose();
  setLowerLeft(cp);
}

void SpatialMatrix::setVelocityShift(const Vector3& origRefPoint,const Vector3& newRefPoint)
{
  setZero();
  setIdentity();
  Matrix3 cp;
  cp.setCrossProduct(origRefPoint-newRefPoint);
  setUpperRight(cp);
}

void SpatialMatrix::setMassMatrix(const Real& mass,const Matrix3& inertia)
{
  setZero();
  for(int i=0;i<3;i++) (*this)(i,i) = mass;
  setLowerRight(inertia);
}




NewtonEulerSolver::NewtonEulerSolver(RobotDynamics3D& _robot)
  :robot(_robot)
{
  robot.GetChildList(children);
  velocities.resize(robot.links.size());
  accelerations.resize(robot.links.size());
  externalWrenches.resize(robot.links.size());
  jointWrenches.resize(robot.links.size());
  for(size_t i=0;i<externalWrenches.size();i++) {
    externalWrenches[i].f.setZero();
    externalWrenches[i].m.setZero();
  }
}

void NewtonEulerSolver::CalcVelocities()
{
  //go down the tree and calculate the velocities
  for(size_t n=0;n<robot.links.size();n++) {
    //get the velocity of the local frame's origin, given it's parent
    int p=robot.parents[n];
    if(p<0) {
      velocities[n].v.setZero();
      velocities[n].w.setZero();
    }
    else {
      velocities[n].v = velocities[p].v + cross(velocities[p].w,robot.links[n].T_World.t-robot.links[p].T_World.t);
      velocities[n].w = velocities[p].w;
    }
    //add on the local velocity
    Real dq=robot.dq(n);
    if(robot.links[n].type == RobotLink3D::Revolute) {
      velocities[n].w += dq*(robot.links[n].T_World.R*robot.links[n].w);
    }
    else {
      velocities[n].v += dq*(robot.links[n].T_World.R*robot.links[n].w);
    }
  }
}

void NewtonEulerSolver::CalcLinkAccel(const Vector& ddq)
{
  CalcVelocities();

  //go down the tree and calculate the accelerations
  for(size_t n=0;n<robot.links.size();n++) {
    //get the velocity of the local frame's origin, given it's parent
    int p=robot.parents[n];
    if(p<0) {
      accelerations[n].v.setZero();
      accelerations[n].w.setZero();
    }
    else {
      const Vector3 &v1=velocities[p].v, &v2=velocities[n].v;
      const Vector3 &w1=velocities[p].w, &w2=velocities[n].w;
      Vector3 odiff = robot.links[n].T_World.t-robot.links[p].T_World.t;
      accelerations[n].v = accelerations[p].v + cross(accelerations[p].w,odiff) + Two*cross(w1,v2-v1) - cross(w1,cross(w1,odiff));
      accelerations[n].w = accelerations[p].w - cross(w2,w1);
    }
    //add on the local acceleration
    Real a=ddq(n);
    if(robot.links[n].type == RobotLink3D::Revolute) {
      accelerations[n].w += a*(robot.links[n].T_World.R*robot.links[n].w);
    }
    else {
      accelerations[n].v += a*(robot.links[n].T_World.R*robot.links[n].w);
    }
  }
}

void NewtonEulerSolver::SetGravityWrenches(const Vector3& gravity)
{
  for(size_t i=0;i<externalWrenches.size();i++) {
    externalWrenches[i].f.mul(gravity,robot.links[i].mass);
    externalWrenches[i].m.setZero();
  }
}

void NewtonEulerSolver::CalcTorques(const Vector& ddq,Vector& t)
{
  CalcLinkAccel(ddq);
  t.resize(robot.links.size());
  
  Vector3 cmLocal,cmWorld;
  Vector3 vcm,wcm;
  Vector3 fcm,mcm;
  Matrix3 Iworld;
  //go down the list backwards... we can do so because parent indices are always < than their children
  for(int n=(int)robot.links.size()-1;n>=0;--n) {
    cmLocal = robot.links[n].T_World.R*robot.links[n].com;
    cmWorld = cmLocal + robot.links[n].T_World.t;
    vcm = accelerations[n].v + cross(accelerations[n].w,cmLocal) + cross(velocities[n].w,cross(velocities[n].w,cmLocal));
    wcm = accelerations[n].w;
    fcm.mul(vcm,robot.links[n].mass);
    robot.links[n].GetWorldInertia(Iworld);
    mcm = Iworld*wcm + cross(velocities[n].w,Iworld*velocities[n].w);  //inertia*accel + gyroscopic force
    
    //subtract the external force
    fcm -= externalWrenches[n].f;
    mcm -= externalWrenches[n].m;

    //add the child wrenches (must be transformed to moment about cm)
    for(size_t i=0;i<children[n].size();i++) {
      int c=children[n][i];
      Assert(c > n);
      fcm += jointWrenches[c].f;
      mcm += jointWrenches[c].m + cross(robot.links[c].T_World.t - cmWorld,jointWrenches[c].f);
    }

    //fcm,mcm must be created by joint force, moment at link origin
    //joint force/moment jf/jm causes moment jm - cmlocal x jf = mcm
    jointWrenches[n].f = fcm;
    jointWrenches[n].m = mcm + cross(cmLocal,fcm);

    //cout<<"Wrench "<<n<<": "<<jointWrenches[n].m<<", "<<jointWrenches[n].f<<endl;
    if(robot.links[n].type == RobotLink3D::Revolute)
      t(n) = dot(jointWrenches[n].m,robot.links[n].T_World.R*robot.links[n].w);
    else
      t(n) = dot(jointWrenches[n].f,robot.links[n].T_World.R*robot.links[n].w);
  }
}


void NewtonEulerSolver::CalcAccel(const Vector& t,Vector& ddq)
{
  ddq.resize(robot.links.size());
  inertiaMatrices.resize(robot.links.size());
  biasingForces.resize(robot.links.size());
  CalcVelocities();

  //initialize the matrices
  Matrix3 Iworld;
  //velocity dependent accelerations (centrifugal and coriolis terms)
  //calculated at center of mass!
  vector<SpatialVector> velDepAccels(robot.links.size());
  for(size_t n=0;n<robot.links.size();n++) {
    //cout<<"Velocity n: "<<velocities[n].v<<", "<<velocities[n].w<<endl;
    //cout<<" Vel at com: "<<velocities[n].v + cross(velocities[n].w,robot.links[n].T_World.R*robot.links[n].com)<<endl;
    robot.links[n].GetWorldInertia(Iworld);
    inertiaMatrices[n].setMassMatrix(robot.links[n].mass,Iworld);
    Vector3 gyroscopicForce = cross(velocities[n].w,Iworld*velocities[n].w);
    int p = robot.parents[n];
    if(p < 0) {
      const Vector3 &w2=velocities[n].w;
      Vector3 com_n_local = robot.links[n].T_World.R*robot.links[n].com;
      Vector3 v = cross(w2,cross(w2,com_n_local));
      velDepAccels[n].set(v,Vector3(Zero));
    }
    else {
      //calculate these quantities about the center of mass of each link
      Vector3 com_n_local = robot.links[n].T_World.R*robot.links[n].com;
      Vector3 com_p_local = robot.links[p].T_World.R*robot.links[p].com;
      Vector3 v1=velocities[p].v+cross(velocities[p].w,com_p_local);
      Vector3 v2=velocities[n].v+cross(velocities[n].w,com_n_local);
      const Vector3 &w1=velocities[p].w, &w2=velocities[n].w;
      Vector3 cdiff = robot.links[n].T_World*robot.links[n].com-robot.links[p].T_World*robot.links[p].com;
      Vector3 v = cross(w1,v2-v1);
      if(robot.links[n].type == RobotLink3D::Prismatic) {
	v += robot.dq(n)*cross(w1,robot.links[p].T_World.R*robot.links[n].w); 
      }
      else {
	v += cross(w2,cross(w2-w1,com_n_local));
      }
      Vector3 w = cross(w1,w2);
      velDepAccels[n].set(v,w);
    }
    biasingForces[n].set(-externalWrenches[n].f,gyroscopicForce-externalWrenches[n].m);
    //cout<<"Velocity dependent accel: "<<velDepAccels[n]<<endl;
  }
  //go backward down the list
  SpatialMatrix cToP,pToC,IaaI,temp,temp2;
  SpatialVector A,Ia,bf_Ivda,vtemp;
  for(int n=(int)robot.links.size()-1;n>=0;--n) {
    if(children[n].empty()) continue;
    Vector3 com_n = robot.links[n].T_World*robot.links[n].com;
    for(size_t i=0;i<children[n].size();i++) {
      int c = children[n][i];
      //compute the transformations
      Vector3 com_c_local =robot.links[c].T_World.R*robot.links[c].com;
      Vector3 com_c = com_c_local + robot.links[c].T_World.t;
      cToP.setForceShift(com_c,com_n);
      pToC.setVelocityShift(com_n,com_c);
      //compute quantities with A
      Vector3 axis_w=robot.links[c].T_World.R*robot.links[c].w;
      if(robot.links[c].type == RobotLink3D::Revolute)
	A.set(cross(axis_w,com_c_local),axis_w);
      else
	A.set(axis_w,Vector3(Zero));
      inertiaMatrices[c].mul(A,Ia);
      Real aIa = Ia.dot(A);
      Assert(aIa > Zero);
      //IaaI.setOuterProduct(Ia);
      for(int i=0;i<6;i++)
	for(int j=0;j<6;j++)
	  IaaI(i,j) = Ia(i)*Ia(j);

      //add this child's contribution to the revised inertia matrix
      //I[n] += cToP*(I-Ia*Iat/aIa)*pToC
      temp = inertiaMatrices[c];
      temp.madd(IaaI,-1.0/aIa);
      temp2.mul(cToP,temp);
      temp.mul(temp2,pToC);
      inertiaMatrices[n] += temp;

      //add this child's contribution to the revised biasing force
      //bf[n] += cToP*(bf+I*vda + I*A*(t[c]-At*(bf+I*vda))/aIa)
      bf_Ivda = biasingForces[c];
      inertiaMatrices[c].madd(velDepAccels[c],bf_Ivda);
      Real At_bf_Ivda = A.dot(bf_Ivda);
      vtemp.mul(Ia,(t[c]-At_bf_Ivda)/aIa);
      vtemp += bf_Ivda;
      cToP.madd(vtemp,biasingForces[n]);
    }
    //cout<<"Revised inertia matrix "<<n<<": "<<endl<<MatrixPrinter(inertiaMatrices[n])<<endl;
    //cout<<"Revised biasing force "<<n<<": "<<endl<<VectorPrinter(biasingForces[n])<<endl;
    //getchar();
  }
  //go down the heirarchy, computing accelerations along the way
  //joint force is X[cm->origin]*(I*a+bf)
  //NOTE: acceleration variables are in com frame, transform them later
  SpatialVector nAccel,pAccel;
  for(size_t n=0;n<robot.links.size();n++) {
    Vector3 com_n_local =robot.links[n].T_World.R*robot.links[n].com;
    //compute quantities with A
    Vector3 axis_w=robot.links[n].T_World.R*robot.links[n].w;
    if(robot.links[n].type == RobotLink3D::Revolute)
      A.set(cross(axis_w,com_n_local),axis_w);
    else
      A.set(axis_w,Vector3(Zero));
    inertiaMatrices[n].mul(A,Ia);
    Real aIa = Ia.dot(A);
    Assert(aIa > Zero);

    int p=robot.parents[n];
    if(p>=0) {
      vtemp.set(accelerations[p].v,accelerations[p].w);
      Vector3 com_n = robot.links[n].T_World*robot.links[n].com;
      Vector3 com_p = robot.links[p].T_World*robot.links[p].com;
      //which way, + or -?
      pToC.setVelocityShift(com_p,com_n);
      pToC.mul(vtemp,pAccel);
    }
    else 
      pAccel.setZero();

    //ddq = (t-A^t*I*X[p->n]*a[p] - A^t(bf+I*vda))/(A^tIA)
    bf_Ivda = biasingForces[n];
    inertiaMatrices[n].madd(velDepAccels[n],bf_Ivda);
    ddq(n) = t(n)-Ia.dot(pAccel);
    ddq(n) -= A.dot(bf_Ivda);
    ddq(n) /= aIa;

    //a = X[p->n]*a[p] + vda + q''*A
    vtemp = velDepAccels[n];
    vtemp.madd(A,ddq(n));
    vtemp += pAccel;
    //vtemp is now the acceleration about the center of mass
    vtemp.get(accelerations[n].v,accelerations[n].w);
    //cout<<"CM acceleration: "<<vn.v<<", "<<vn.w<<endl;
  }

  for(size_t n=0;n<robot.links.size();n++) {
    Vector3 com_n_local =robot.links[n].T_World.R*robot.links[n].com;

    //transform to origin
    RigidBodyVelocity vn=accelerations[n];
    accelerations[n].setShifted(vn,-com_n_local);

    SpatialVector jointForce = biasingForces[n];
    inertiaMatrices[n].madd(vtemp,jointForce);
    //now jointForce is in the com frame
    Wrench fn;
    jointForce.get(fn.f,fn.m);
    //transform to joint frame
    jointWrenches[n].setShifted(fn,-com_n_local);
  }
}

void NewtonEulerSolver::CalcKineticEnergyMatrix(Matrix& B)
{
  //by virtue of the relationship
  //Bq'' + C + G = t
  //if we let q'' = ei, we can get column i of B (denoted Bi)
  //such that Bi = t-C-G
  //C+G can be obtained by setting q''=0, then t = C+G
  B.resize(robot.links.size(),robot.links.size());
  Vector t0,t;
  Vector ddq(robot.links.size());
  ddq.setZero();
  CalcTorques(ddq,t0);
  for(int i=0;i<ddq.n;i++) {
    ddq(i) = 1;
    CalcTorques(ddq,t);
    t -= t0;
    B.copyCol(i,t);
    ddq(i) = 0;
  }
}

void NewtonEulerSolver::MulKineticEnergyMatrix(const Vector& x,Vector& Bx)
{
  Assert(x.n == (int)robot.links.size());
  Vector t0;
  Vector ddq(robot.links.size());
  ddq.setZero();
  CalcTorques(ddq,t0);
  CalcTorques(x,Bx);
  Bx -= t0;
}

void NewtonEulerSolver::MulKineticEnergyMatrix(const Matrix& A,Matrix& BA)
{
  Assert(A.m == (int)robot.links.size());
  Vector t0;
  Vector ddq(robot.links.size());
  ddq.setZero();
  CalcTorques(ddq,t0);
  BA.resize(A.m,A.n);
  for(int i=0;i<A.n;i++) {
    Vector Ai,BAi;
    A.getColRef(i,Ai);
    BA.getColRef(i,BAi);
    CalcTorques(Ai,BAi);
    BAi -= t0;
  }
}

void NewtonEulerSolver::SelfTest()
{
  cout<<"NewtonEulerSolver::SelfTest() first testing basic stuff."<<endl;
  {
    Real h=1e-3,tol=1e-2;
    //let Jp = jacobian of position/orientation, col j=dp/dqj
    //dJp/dqi = hessian = d^2p/dqidqj
    Matrix J0,J1;
    Matrix H1,H2,H3,H4,H5,H6;
    Matrix* Ho[3]={&H1,&H2,&H3};
    Matrix* Hp[3]={&H4,&H5,&H6};
    for(size_t m=0;m<robot.links.size();m++) {
      robot.UpdateFrames();
      robot.GetJacobianDeriv(robot.links[m].com,m,Ho,Hp);
      robot.GetFullJacobian(robot.links[m].com,m,J0);
      for(size_t i=0;i<robot.links.size();i++) {
	Real oldq = robot.q(i);
	robot.q(i) += h;
	robot.UpdateFrames();
	robot.GetFullJacobian(robot.links[m].com,m,J1);
	J1 -= J0;
	J1 *= 1.0/h;
	Matrix Hi(6,robot.links.size());
	Vector temp;
	H1.getColRef(i,temp); Hi.copyRow(0,temp);
	H2.getColRef(i,temp); Hi.copyRow(1,temp);
	H3.getColRef(i,temp); Hi.copyRow(2,temp);
	H4.getColRef(i,temp); Hi.copyRow(3,temp);
	H5.getColRef(i,temp); Hi.copyRow(4,temp);
	H6.getColRef(i,temp); Hi.copyRow(5,temp);
	if(!J1.isEqual(Hi,tol*Max(Hi.maxAbsElement(),1.0))) {
	  cerr<<"Error computing hessian!"<<endl;
	  cerr<<"dp"<<m<<"/dq"<<i<<":"<<endl;
	  cerr<<MatrixPrinter(Hi,MatrixPrinter::AsciiShade)<<endl;
	  cerr<<"diff"<<endl;
	  cerr<<MatrixPrinter(J1,MatrixPrinter::AsciiShade)<<endl;
	  cerr<<"Error: "<<endl;
	  J1 -= Hi;
	  cerr<<MatrixPrinter(J1,MatrixPrinter::AsciiShade)<<endl;
	  Abort();
	}
	robot.q(i) = oldq;
      }
    }
    

    Matrix B0,B1,dB;
    robot.UpdateFrames();
    robot.UpdateDynamics();
    robot.GetKineticEnergyMatrix(B0);
    for(size_t i=0;i<robot.links.size();i++) {
      Real oldq = robot.q(i);
      robot.GetKineticEnergyMatrixDeriv(i,dB);
      robot.q(i) += h;
      robot.UpdateFrames();
      robot.UpdateDynamics();
      robot.GetKineticEnergyMatrix(B1);
      robot.q(i) = oldq;

      B1 -= B0;
      B1 *= 1.0/h;
      if(!B1.isEqual(dB,tol*Max(dB.maxAbsElement(),1.0))) {
	cerr<<"Error computing kinetic energy matrix derivative!"<<endl;
	cerr<<"dB/dq"<<i<<":"<<endl;
	cerr<<MatrixPrinter(dB,MatrixPrinter::AsciiShade)<<endl;
	cerr<<"dB diff"<<endl;
	cerr<<MatrixPrinter(B1,MatrixPrinter::AsciiShade)<<endl;
	cerr<<"Error: "<<endl;
	B1 -= dB;
	cerr<<MatrixPrinter(B1,MatrixPrinter::AsciiShade)<<endl;
	Abort();
      }
    }
    robot.UpdateFrames();
    robot.UpdateDynamics();
  }

  //check velocity computation
  CalcVelocities();
  Vector3 v,w;
  for(size_t i=0;i<velocities.size();i++) {
    robot.GetWorldVelocity(Vector3(Zero),i,robot.dq,v);
    robot.GetWorldAngularVelocity(i,robot.dq,w);
    Assert(v.isEqual(velocities[i].v,1e-3));
    Assert(w.isEqual(velocities[i].w,1e-3));
  }
  cout<<"NewtonEulerSolver::SelfTest() Passed velocity test."<<endl;

  //check acceleration computation
  Vector ddq(robot.links.size());
  Vector t,ddqtest;
  //fill with random values
  for(int i=0;i<ddq.n;i++)
    ddq(i) = Rand(-One,One);
  CalcLinkAccel(ddq);
  vector<RigidBodyVelocity> oldVels=velocities;
  Vector oldDq=robot.dq;
  Vector oldq=robot.q;
  
  Real h=1e-3;
  vector<RigidBodyVelocity> v1,v2,adiff;
  robot.q.madd(oldDq,h);
  robot.dq.madd(ddq,h);
  robot.UpdateFrames();
  CalcVelocities();
  v2=velocities;

  robot.q.madd(oldDq,-Two*h);
  robot.dq.madd(ddq,-Two*h);
  robot.UpdateFrames();
  CalcVelocities();
  v1=velocities;
  
  adiff.resize(v1.size());
  for(size_t i=0;i<adiff.size();i++) {
    adiff[i].v = (v2[i].v-v1[i].v)/(Two*h);
    adiff[i].w = (v2[i].w-v1[i].w)/(Two*h);
  }

  Real tol=1e-3;
  for(size_t i=0;i<adiff.size();i++) {
    if(!adiff[i].v.isEqual(accelerations[i].v,tol*Max(1.0,accelerations[i].v.maxAbsElement()))) {
      cout<<"Error computing linear acceleration at link "<<i<<": "<<accelerations[i].v<<" vs. "<<adiff[i].v<<endl;
      int p=robot.parents[i];
      if(p != -1) {
	cout<<"Linear velocity is "<<oldVels[i].v<<", angular "<<oldVels[i].w<<endl;
	cout<<"Parent's linear is "<<oldVels[p].v<<", angular "<<oldVels[p].w<<endl;
      }
      getchar();
    }
    if(!adiff[i].w.isEqual(accelerations[i].w,tol*Max(1.0,accelerations[i].w.maxAbsElement()))) {
      cout<<"Error computing angular acceleration at link "<<i<<": "<<accelerations[i].w<<" vs. "<<adiff[i].w<<endl;
      int p=robot.parents[i];
      if(p != -1) {
	cout<<"Linear velocity is "<<oldVels[i].v<<", angular "<<oldVels[i].w<<endl;
	cout<<"Parent's linear is "<<oldVels[p].v<<", angular "<<oldVels[p].w<<endl;
      }
      getchar();
    }
  }
  robot.q=oldq;
  robot.dq=oldDq;
  robot.UpdateFrames();
  velocities=oldVels;
  cout<<"NewtonEulerSolver::SelfTest() Passed acceleration test."<<endl;

  //check coriolis forces (set ddq = G = 0)
  SetGravityWrenches(Vector3(Zero));
  ddq.setZero();
  CalcTorques(ddq,t);
  Vector C;
  robot.UpdateDynamics();
  robot.GetCoriolisForces(C);
  if(!t.isEqual(C,1e-3)) {
    cerr<<"Coriolis forces computed traditionally do not match with"<<endl
	<<"newton-euler!"<<endl;
    cerr<<"N-E: "<<VectorPrinter(t)<<endl;
    cerr<<"Traditional: "<<VectorPrinter(C)<<endl;
    t -= C;
    cerr<<"Error: "<<VectorPrinter(t)<<endl;
    Abort();
  }
  cout<<"NewtonEulerSolver::SelfTest() Passed coriolis force test."<<endl;

  //check forward dynamics
  ddq.setZero();
  ddq(0) = 1.0;
  //for(int i=0;i<ddq.n;i++)
    //ddq(i) = Rand(-One,One);
  //SetGravityWrenches(Vector3(0,0,-9.8));
  for(int i=0;i<ddq.n;i++) {
    ddq(i) = 1.0;
    CalcTorques(ddq,t);
    //cout<<"Torques: "<<t<<endl;
    CalcAccel(t,ddqtest);
    if(!ddq.isEqual(ddqtest,1e-4)) {
      cerr<<"Forward dynamics is not an exact inverse of inverse dynamics!"<<endl;
      cerr<<"Desired ddq: "<<ddq<<endl;
      cerr<<"Resulting: "<<ddqtest<<endl;
      ddqtest -= ddq;
      cerr<<"Error: "<<ddqtest<<endl;
      Abort();
    }
  }
  cout<<"NewtonEulerSolver::SelfTest() Passed forward dynamics test."<<endl;

  //check kinetic energy computations
  Matrix B,Btrue,Btemp;
  CalcKineticEnergyMatrix(B);
  robot.GetKineticEnergyMatrix(Btrue);
  if(!B.isEqual(Btrue,1e-3)) {
    cerr<<"Kinetic energy matrix computed with newton-euler doesn't"<<endl
	<<"match that of traditional method!"<<endl;
    cerr<<"N-E:"<<endl;
    cerr<<MatrixPrinter(B,MatrixPrinter::AsciiShade)<<endl;
    cerr<<"Traditional:"<<endl;
    cerr<<MatrixPrinter(Btrue,MatrixPrinter::AsciiShade)<<endl;
    cerr<<"Error: "<<endl;
    Btemp.sub(Btrue,B);
    cerr<<MatrixPrinter(Btemp,MatrixPrinter::AsciiShade)<<endl;
    Abort();
  }
  cout<<"NewtonEulerSolver::SelfTest() Passed kinetic energy matrix test."<<endl;

  //check inverse kinetic energy computations
  Matrix Binv;
  CalcKineticEnergyMatrixInverse(Binv);
  Btemp.mul(Btrue,Binv);
  for(int i=0;i<ddq.n;i++) Btemp(i,i)-=1.0;
  if(!Btemp.isZero(1e-3)) {
    cerr<<"Inverse kinetic energy matrix computed with newton-euler"<<endl
	<<"doesn't match that of traditional method!"<<endl;
    cerr<<"N-E:"<<endl;
    cerr<<MatrixPrinter(Binv,MatrixPrinter::AsciiShade)<<endl;
    cerr<<"Traditional:"<<endl;
    Matrix BinvTrue;
    CholeskyDecomposition<Real> chol(Btrue);
    chol.getInverse(BinvTrue);
    cerr<<MatrixPrinter(BinvTrue,MatrixPrinter::AsciiShade)<<endl;
    cerr<<"Error: "<<endl;
    cerr<<MatrixPrinter(Btemp,MatrixPrinter::AsciiShade)<<endl;
    Abort();
  }
  cout<<"NewtonEulerSolver::SelfTest() Passed kinetic energy inverse test."<<endl;
  cout<<"NewtonEulerSolver::SelfTest() Done!"<<endl;
}

void NewtonEulerSolver::CalcKineticEnergyMatrixInverse(Matrix& Binv)
{
  //by virtue of the relationship
  //Bq'' + C + G = t
  //q'' = B^-1(t-C-G)
  //if we let t = ei, we can get column i of B^-1 (denoted Bi)
  //such that Bi = t-B^-1(C+G)
  //-B^-1(C+G) can be obtained by setting t=0, then q'' = -B^-1(C+G)
  Binv.resize(robot.links.size(),robot.links.size());
  Vector t(robot.links.size());
  Vector ddq0,ddq;
  t.setZero();
  CalcAccel(t,ddq0);
  for(int i=0;i<t.n;i++) {
    t(i) = 1;
    CalcAccel(t,ddq);
    ddq -= ddq0;
    Binv.copyCol(i,ddq);
    t(i) = 0;
  }
}

void NewtonEulerSolver::MulKineticEnergyMatrixInverse(const Vector& x,Vector& Binvx)
{
  Assert(x.n == (int)robot.links.size());
  Vector t(robot.links.size());
  Vector ddq0;
  t.setZero();
  CalcAccel(t,ddq0);
  CalcAccel(x,Binvx);
  Binvx -= ddq0;
}

void NewtonEulerSolver::MulKineticEnergyMatrixInverse(const Matrix& A,Matrix& BinvA)
{
  Assert(A.m == (int)robot.links.size());
  Vector t(robot.links.size());
  Vector ddq0;
  t.setZero();
  CalcAccel(t,ddq0);
  BinvA.resize(A.m,A.n);
  for(int i=0;i<A.n;i++) {
    Vector Ai,BAi;
    A.getColRef(i,Ai);
    BinvA.getColRef(i,BAi);
    CalcAccel(Ai,BAi);
    BAi -= ddq0;
  }
}

void NewtonEulerSolver::CalcResidualTorques(Vector& CG)
{
  Vector ddq(robot.links.size(),Zero);
  CalcTorques(ddq,CG);
}

void NewtonEulerSolver::CalcResidualAccel(Vector& ddq0)
{
  Vector t(robot.links.size(),Zero);
  CalcAccel(t,ddq0);
}

