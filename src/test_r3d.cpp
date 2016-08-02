#include "omega_h_r3d.hpp"

#include <iostream>

int main() {
  osh::Vector<3> verts[4] = {
    {0,0,0},
    {1,0,0},
    {0,1,0},
    {0,0,1}
  };
  std::cout << "volume is " << osh::r3d::orient(verts) << '\n';
  osh::r3d::Plane<3> faces[4];
  osh::r3d::tet_faces_from_verts(faces, verts);
  for (osh::Int i = 0; i < 4; ++i) {
    std::cout << "plane #" << i << " is "
      << "(" << faces[i].n[0]
      << "," << faces[i].n[1]
      << "," << faces[i].n[2]
      << ") * " << faces[i].d << '\n';
  }
}