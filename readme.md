## ARAP deformation
### Description
This is a dirty implementation of <a href="https://igl.ethz.ch/projects/ARAP/arap_web.pdf">As-Rigid-As-Possible Surface Modeling (SGP2007)</a> using uniform laplacian

Use **local/global** method to optimize:
$$
\sum_{i}\sum_{j\in N(i)}w_{ij}||(p'_i-p'_j)-R_i(p_i-p_j)||^2\ (1)
$$
local optimization (fixed p' (known) to get best $R_1,...,R_n$, use SVD decomposition to **construct** each $R_i$):

for each $i$ to optimize this:
$$
\sum_{j\in N(i)}w_{ij}||e'_{ij}-R_ie_{ij}||^2
$$
we need $R_i=V_iU_i^T$, and $U_i, V_i$ is the SVD decomposition of: (note that $e_{ij}$ is the edge on the origin mesh, no matter how many iterations)
$$
\sum_{j\in N(i)}w_{ij}e_{ij}e'^T_{ij}=U_iS_iV_i^T
$$
global optimization (fixed R (known) to get best $p_1,...,p_n$ by solving a **linear system**), take the derivative of (1) and assume R as constant, we can get i-th equation of the system:

<img src="fonp.png"/>

solving p' and R alternatively to get the best solution of p'.

### Usage
Work in vs2017 release mode, after hitting the generate solution button, shader files need to be copied into `x64/Release` to make it work, use command `./arap.exe your/path/to/the/mesh.obj`

Press `w` to toggle wire mode

Press `d` to toggle deform/select mode

Press `middle mouse button` to save current mesh

Press `a` to display/hide anchor points

Drag `left mouse button` to rotate mesh

Drag `right mouse button` to select/deform in select/deform mode
### Cotangent laplacian
When I use cotangent laplacian in the deformation iteration, I got really wired result, and found it hard to make it right.
