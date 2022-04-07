## ARAP deformation
### Description
This is a dirty implementation of <a href="https://igl.ethz.ch/projects/ARAP/arap_web.pdf">As-Rigid-As-Possible Surface Modeling (SGP2007)</a> using uniform laplacian

Use **local/global** method to optimize energy at each vertex i:

<img src="https://user-images.githubusercontent.com/42519504/162159684-6999ad50-4c9c-4e8f-bf9c-dd28e47fff48.PNG"/>

**local optimization** (fixed p' (known) to get best R1,...,Rn, use SVD decomposition to **construct** each Ri):

for each i to optimize this (let pi - pj = eij, p'i - p'j  = e'ij):

<img src="https://user-images.githubusercontent.com/42519504/162159625-74cc27a3-3522-46d0-b8e4-3b56d14fc560.png">

so we need Ri=Vi*transpose(Ui) to let the stuff in `(...)` to be semi-positive definite symmetric to make its trace maximum, and Ui, Vi is the SVD decomposition of: (note that eij is the edge on the origin mesh, no matter how many iterations)

<img src="https://user-images.githubusercontent.com/42519504/162159515-85a0eb95-d27b-4943-983d-eec7d754cda0.PNG">

**global optimization** (fixed R (known) to get best p1,...,pn by solving a **linear system**), take the derivative of (1) and assume R as constant, we can get i-th equation of the system:

<img src="https://user-images.githubusercontent.com/42519504/162156056-84f81c75-10e4-4230-b320-548ecb17a581.PNG"/>

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
