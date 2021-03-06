#include "global.h"
#include "shader.h"
#include "camera.h"
#include "stb_image.h"

/////////////////////// BVH /////////////////////////
bvh::Bvh<float> BVH;
vector<bvh::Triangle<float>> triangles;
void build() {
	auto bbox_center = bvh::compute_bounding_boxes_and_centers(triangles.data(), triangles.size());
	auto global_bbox = bvh::compute_bounding_boxes_union(bbox_center.first.get(), triangles.size());

	bvh::SweepSahBuilder<bvh::Bvh<float>> builder(BVH);
	builder.build(global_bbox, bbox_center.first.get(), bbox_center.second.get(), triangles.size());
}

bool intersect(bvh::Ray<float> &ray, pair<int, float> &res) {
	bvh::ClosestPrimitiveIntersector<bvh::Bvh<float>, bvh::Triangle<float>> intersector(BVH, triangles.data());
	bvh::SingleRayTraverser<bvh::Bvh<float>> traverser(BVH);

	if (auto hit = traverser.traverse(ray, intersector)) {
		auto triangle_index = hit->primitive_index;
		auto intersection = hit->intersection;
		res = { triangle_index, intersection.t };
		return true;
	}
	return false;
}

/////////////////////// MESH ////////////////////////
MatrixXd V;
MatrixXd oV; // origin mesh == lookup mesh, P in arap
MatrixXi F;
map<int, set<int>> H;
map<int, set<pair<int, int>>> VF;
int n;
vector<float> vertices;
vector<float> normals;
vector<Vector3f> t_normal;
vector<int> t_normal_cnt;
vector<vector<int>> anchors;
unsigned mesh_vbo, mesh_vao, normal_vbo;
unsigned anchor_vao, anchor_vbo;
unsigned tex_id;

void update_bvh() {
	for (int i = 0; i < F.rows(); i++) {
		bvh::Vector3<float> p[3];
		for (int j = 0; j < 3; j++) {
			p[j] = {
				(float)V(F(i, j), 0),
				(float)V(F(i, j), 1),
				(float)V(F(i, j), 2)
			};
		}
		triangles[i] = bvh::Triangle<float>(p[0], p[1], p[2]);
	}
	build();
}

void update_mesh_buffer() {
	for (int i = 0, cnt = 0; i < F.rows(); i++) {
		for (int j = 0; j < 3; j++) {
			vertices[cnt++] = V(F(i, j), 0);
			vertices[cnt++] = V(F(i, j), 1);
			vertices[cnt++] = V(F(i, j), 2);
		}
	}
	glBindBuffer(GL_ARRAY_BUFFER, mesh_vbo);
	float *model_vtx_ptr = (float*)glMapBuffer(GL_ARRAY_BUFFER, GL_WRITE_ONLY);
	memcpy(model_vtx_ptr, vertices.data(), vertices.size() * sizeof(float));
	glUnmapBuffer(GL_ARRAY_BUFFER);
}

void update_normal_buffer() {
	t_normal.resize(V.rows(), Vector3f::Zero());
	t_normal_cnt.resize(V.rows(), 0);
	for (int i = 0; i < F.rows(); i++) {
		Vector3f p0, p1, p2;
		for (int j = 0; j < 3; j++) {
			p0[j] = V(F(i, 0), j);
			p1[j] = V(F(i, 1), j);
			p2[j] = V(F(i, 2), j);
		}
		Vector3f v1 = p1 - p0, v2 = p2 - p0;
		Vector3f norm = v1.cross(v2).normalized();
		for (int j = 0; j < 3; j++) {
			t_normal[F(i, j)] += norm;
			t_normal_cnt[F(i, j)]++;
		}
	}
	
	for (int i = 0; i < t_normal.size(); i++) t_normal[i] /= t_normal_cnt[i];
	
	for (int i = 0, cnt = 0; i < F.rows(); i++) {
		for (int j = 0; j < 3; j++) {
			for (int k = 0; k < 3; k++) {
				normals[cnt++] = t_normal[F(i, j)][k];
			}
		}
	}
	

	glBindBuffer(GL_ARRAY_BUFFER, normal_vbo);
	float *normal_ptr = (float*)glMapBuffer(GL_ARRAY_BUFFER, GL_WRITE_ONLY);
	memcpy(normal_ptr, normals.data(), normals.size() * sizeof(float));
	glUnmapBuffer(GL_ARRAY_BUFFER);
}

void update_anchor_buffer() {
	glBindBuffer(GL_ARRAY_BUFFER, anchor_vbo);
	float *anchor_vtx_ptr = (float*)glMapBuffer(GL_ARRAY_BUFFER, GL_WRITE_ONLY);
	for (int i = 0, cnt = 0; i < anchors.size(); i++) {
		for (auto t : anchors[i]) {
			anchor_vtx_ptr[cnt++] = V(t, 0);
			anchor_vtx_ptr[cnt++] = V(t, 1);
			anchor_vtx_ptr[cnt++] = V(t, 2);
			anchor_vtx_ptr[cnt++] = i;
		}
	}
	glUnmapBuffer(GL_ARRAY_BUFFER);
}

/////////////////////// ARAP ////////////////////////
vector<MatrixXf> R;
MatrixXd iteration() {
	for (auto v : H) {
		MatrixXf m = MatrixXf::Zero(3, 3);
		for (auto vv : v.second) {
			Vector3f e, e_;
			for (int i = 0; i < 3; i++) {
				e[i] = oV(v.first, i) - oV(vv, i);
				e_[i] = V(v.first, i) - V(vv, i);
			}
			m += e * e_.transpose();
		}
		JacobiSVD<MatrixXf> svd(m, ComputeFullU | ComputeFullV);
		R[v.first] = svd.matrixV() * (svd.matrixU().transpose());
	}

	int cnt = 0;
	for (auto t : anchors) cnt += t.size();

	SparseMatrix<double> B(cnt + n, 3), A(cnt + n, n);
	vector<Triplet<double>> tA, tB;
	for (auto v : H) {
		Vector3f p = Vector3f::Zero();
		for (auto vv : v.second) {
			tA.push_back(Triplet<double>(v.first, vv, -1));
			Vector3f e(
				oV(v.first, 0) - oV(vv, 0),
				oV(v.first, 1) - oV(vv, 1),
				oV(v.first, 2) - oV(vv, 2)
			);
			p += 0.5 * (R[v.first] + R[vv]) * e;
		}
		tA.push_back(Triplet<double>(v.first, v.first, v.second.size()));
		for (int i = 0; i < 3; i++) tB.push_back(Triplet<double>(v.first, i, p[i]));
	}
	int sum = 0;
	for (int i = 0; i < anchors.size(); i++) {
		for (int j = 0; j < anchors[i].size(); j++) {
			Vector3f p(
				V(anchors[i][j], 0),
				V(anchors[i][j], 1),
				V(anchors[i][j], 2)
			);
			tA.push_back(Triplet<double>(n + sum, anchors[i][j], 1));
			for (int k = 0; k < 3; k++) tB.push_back(Triplet<double>(n + sum, k, p[k]));
			sum++;
		}
	}

	A.setFromTriplets(tA.begin(), tA.end());
	B.setFromTriplets(tB.begin(), tB.end());

	SparseMatrix<double> ATA = A.transpose() * A;
	SparseMatrix<double> ATB = A.transpose() * B;

	SimplicialLLT<SparseMatrix<double>> llt(ATA);
	MatrixXd res = llt.solve(ATB);
	return res;
}

float error(MatrixXd &P) {
	double res = 0.0;
	for (int i = 0; i < n; i++)
		for (int j = 0; j < 3; j++)
			res = max(res, fabs(P(i, j) - V(i, j)));
	return res;
}
void draw();
void solve() {
	double ERR = 1e9;
	int iter = 0;
	cout << "-----------Start----------" << endl;
	while (ERR > 1e-2) {
		auto res = iteration();
		ERR = min(ERR, error(res));
		V = res;
		cout << "iter-->" << ++iter << ", " << "err-->" << ERR << endl;
		update_mesh_buffer();
		update_normal_buffer();
		update_anchor_buffer();
		update_bvh();
		draw();
	}
}

/////////////////////// opengl ///////////////////////
GLFWwindow *window;
int width = 1280, height = 720;
glm::mat4 Model, View, Projection, drag_rotation;
bool drag = false;
glm::vec2 drag_start;
bool deform = false;
bool changed = false;
int group = -1;
glm::vec3 offset;
glm::vec2 deform_start;
camera cam(glm::vec3(0.0f), 5.0, glm::vec3(0.0f, 1.0f, 0.0f), width, height, 45.0, 0.1, 200, 90, 0);
Shader model_shader;
Shader anchor_shader;
Shader wire_shader;
bool deform_mode = false;
bool select;
int click_cnt;
map<int, bool> chosen;
bool wired = true;
bool anced = true;
bool solving = false;

int ray_tracer(double xpos, double ypos, bvh::Vector3<float> &point) {
	double x_ndc, y_ndc, z_ndc;
	x_ndc = 2 * xpos / width - 1.0;
	y_ndc = 1.0 - 2 * ypos / height;
	glm::vec4 clip_pos(x_ndc, y_ndc, 1, 1);
	glm::mat4 inv_mvp = glm::inverse(Projection * View * Model);
	glm::vec4 model_pos = inv_mvp * clip_pos;
	if (model_pos.w != 0.0) model_pos /= model_pos.w;
	glm::vec4 from = glm::inverse(Model) * glm::vec4(cam.position, 1.0);
	glm::vec4 dir = model_pos - from;
	bvh::Ray<float> ray(
		bvh::Vector3<float>(from[0], from[1], from[2]),
		bvh::Vector3<float>(dir[0], dir[1], dir[2])
	);
	pair<int, float> hit;
	if (intersect(ray, hit)) {
		point = ray.origin + hit.second * ray.direction;
		return hit.first;
	}
	return -1;
}

int dist(bvh::Vector3<float> &a, bvh::Vector3<float> &b) {
	double ans = 0;
	for (int i = 0; i < 3; i++) ans += pow(a[i] - b[i], 2);
	return sqrt(ans);
}

int closest_group(const bvh::Vector3<float> &point) {
	for (int i = 0; i < anchors.size(); i++) {
		for (auto t : anchors[i]) {
			bool flag = 1;
			for (int j = 0; j < 3; j++)
				if (abs(V(t, j) - point[j]) > 0.1) flag = 0;
			if (flag) return i;
		}
	}
	return -1;
}

void mouse_button_callback(GLFWwindow* window, int button, int action, int mods) {
	if (solving) return;
	if (button == GLFW_MOUSE_BUTTON_LEFT) {
		if (action == GLFW_PRESS) {
			drag = true;
			double xpos, ypos;
			glfwGetCursorPos(window, &xpos, &ypos);
			drag_start = glm::vec2(xpos, ypos);
		}
		else if (action == GLFW_RELEASE) {
			drag = false;
			Model = drag_rotation * Model;
			drag_rotation = glm::mat4(1.0f);
		}
	}else if (button == GLFW_MOUSE_BUTTON_RIGHT) {
		if (deform_mode) {
			if (action == GLFW_PRESS) {
				double xpos, ypos;
				glfwGetCursorPos(window, &xpos, &ypos);
				bvh::Vector3<float> point;
				if (~ray_tracer(xpos, ypos, point)) {
					group = closest_group(point);
					if (group != -1) {
						deform = true;
						deform_start = { xpos, ypos };
					}
				}
			}
			else if (action == GLFW_RELEASE) {
				if (deform) {
					for (auto t : anchors[group]) {
						V(t, 0) += offset[0] * 0.0005;
						V(t, 1) += offset[1] * 0.0005;
						V(t, 2) += offset[2] * 0.0005;
					}

					deform = false;
					group = -1;
					solving = true;
					solve();
					solving = false;
				}
			}
		}
		else {
			if (action == GLFW_PRESS) {
				select = true;
				anchors.push_back(vector<int>());
			}
			else if (action == GLFW_RELEASE) {
				select = false;
				if(anchors.back().size()) update_anchor_buffer();
				else anchors.pop_back();
			}
		}
	}
	else if (button == GLFW_MOUSE_BUTTON_MIDDLE && action == GLFW_PRESS) {
		if (igl::writeOBJ("./this-is-an-output-file.obj", V, F)) {
			cout << "Saved" << endl;
		}
		else {
			cout << "Save error" << endl;
		}
		
	}
}

void cursor_position_callback(GLFWwindow* window, double xpos, double ypos) {
	if (solving) return;
	if (drag) {
		glm::mat3 to_world = glm::inverse(glm::mat3(View));
		glm::vec2 drag_vec = glm::vec2(xpos - drag_start.x, drag_start.y - ypos);

		glm::vec3 axis_vec = glm::normalize(to_world * glm::vec3(-drag_vec.y, drag_vec.x, 0));
		float angle = glm::length(drag_vec) / height / 2 * PI;

		drag_rotation = glm::rotate(glm::mat4(1.0f), angle, axis_vec);
	}else if (deform) {
		glm::mat3 to_model = glm::inverse(glm::mat3(Model));
		offset = to_model * (cam.right * float(xpos - deform_start.x) + cam.up * float(deform_start.y - ypos));
		glBindBuffer(GL_ARRAY_BUFFER, mesh_vbo);
		float *model_vtx_ptr = (float*)glMapBuffer(GL_ARRAY_BUFFER, GL_WRITE_ONLY);
		for (auto t : anchors[group]) {
			for (auto[i, j] : VF[t]) {
				model_vtx_ptr[i * 9 + j * 3] = V(t, 0) + offset[0] * 0.0005;
				model_vtx_ptr[i * 9 + j * 3 + 1] = V(t, 1) + offset[1] * 0.0005;
				model_vtx_ptr[i * 9 + j * 3 + 2] = V(t, 2) + offset[2] * 0.0005;
			}
		}
		glUnmapBuffer(GL_ARRAY_BUFFER);

		glBindBuffer(GL_ARRAY_BUFFER, anchor_vbo);
		float *anchor_vtx_ptr = (float*)glMapBuffer(GL_ARRAY_BUFFER, GL_WRITE_ONLY);
		int cnt = 0;
		for (int i = 0; i < group; i++) cnt += anchors[i].size();
		for (auto t : anchors[group]) {
			anchor_vtx_ptr[cnt * 4] = V(t, 0) + offset[0] * 0.0005;
			anchor_vtx_ptr[cnt * 4 + 1] = V(t, 1) + offset[1] * 0.0005;
			anchor_vtx_ptr[cnt * 4 + 2] = V(t, 2) + offset[2] * 0.0005;
			cnt++;
		}
		glUnmapBuffer(GL_ARRAY_BUFFER);

	}else if (select) {
		double xpos, ypos;
		glfwGetCursorPos(window, &xpos, &ypos);
		bvh::Vector3<float> point;
		int id = ray_tracer(xpos, ypos, point);
		if (~id) {
			double min_dist = 1e9;
			int index = 0;
			for (int i = 0; i < 3; i++) {
				bvh::Vector3<float> p;
				p[0] = V(F(id, i), 0);
				p[1] = V(F(id, i), 1);
				p[2] = V(F(id, i), 2);
				double d = dist(point, p);
				if (d < min_dist) {
					min_dist = d;
					index = F(id, i);
				}
			}
			if (chosen.count(index) == 0) {
				anchors.back().push_back(index);
				chosen[index] = 1;
			}
		}
	}
}

void scroll_callback(GLFWwindow* window, double xoffset, double yoffset) {
	cam.fov -= (float)yoffset;
	if (cam.fov < 1.0f)
		cam.fov = 1.0f;
	if (cam.fov > 180.0f)
		cam.fov = 180.0f;
	Projection = cam.projection();
}
void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods) {
	if (solving) return;
	if (key == GLFW_KEY_D && action == GLFW_RELEASE) {
		deform_mode ^= 1;
		if (deform_mode) glfwSetWindowTitle(window, "Hikari(*deform)");
		else glfwSetWindowTitle(window, "Hikari(*select)");
	}
	if (key == GLFW_KEY_W && action == GLFW_RELEASE) wired ^= 1;
	if (key == GLFW_KEY_A && action == GLFW_RELEASE) anced ^= 1;
	if (key == GLFW_KEY_Z && action == GLFW_RELEASE) {
		if (anchors.size()) {
			for (auto t : anchors.back()) chosen.erase(t);
			anchors.pop_back();
			update_anchor_buffer();
		}
	}
}

int glfw() {
	glfwInit();
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
	glfwWindowHint(GLFW_RESIZABLE, GL_FALSE);
	window = glfwCreateWindow(width, height, "Hikari(*select)", nullptr, nullptr);
	glfwSetMouseButtonCallback(window, mouse_button_callback);
	glfwSetCursorPosCallback(window, cursor_position_callback);
	glfwSetScrollCallback(window, scroll_callback);
	glfwSetKeyCallback(window, key_callback);
	if (window == nullptr) {
		std::cout << "Failed to create GLFW window" << std::endl;
		glfwTerminate();
		return -1;
	}
	glfwMakeContextCurrent(window);
	if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
		std::cout << "Failed to initialize GLAD" << std::endl;
		return -1;
	}
	return 0;
}

void bind_data() {
	glGenTextures(1, &tex_id);
	glBindTexture(GL_TEXTURE_2D, tex_id);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	int w, h, c;
	//stbi_set_flip_vertically_on_load(true);
	unsigned char *data = stbi_load("./mat.png", &w, &h, &c, 0);
	if (c == 3) glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, w, h, 0, GL_RGB, GL_UNSIGNED_BYTE, data);
	if (c == 4) glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
	
	glGenerateMipmap(GL_TEXTURE_2D);
	stbi_image_free(data);

	glGenBuffers(1, &mesh_vbo);
	glBindBuffer(GL_ARRAY_BUFFER, mesh_vbo);
	glBufferData(GL_ARRAY_BUFFER, F.rows() * 9 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);

	glGenBuffers(1, &normal_vbo);
	glBindBuffer(GL_ARRAY_BUFFER, normal_vbo);
	glBufferData(GL_ARRAY_BUFFER, F.rows() * 9 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);

	glGenVertexArrays(1, &mesh_vao);
	glBindVertexArray(mesh_vao);
	glBindBuffer(GL_ARRAY_BUFFER, mesh_vbo);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
	glBindBuffer(GL_ARRAY_BUFFER, normal_vbo);
	glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
	glEnableVertexAttribArray(0);
	glEnableVertexAttribArray(1);
	glBindBuffer(GL_ARRAY_BUFFER, 0);

	glGenBuffers(1, &anchor_vbo);
	glBindBuffer(GL_ARRAY_BUFFER, anchor_vbo);
	glBufferData(GL_ARRAY_BUFFER, V.rows() * sizeof(float) * 4, nullptr, GL_DYNAMIC_DRAW);

	glGenVertexArrays(1, &anchor_vao);
	glBindVertexArray(anchor_vao);
	glBindBuffer(GL_ARRAY_BUFFER, anchor_vbo);
	glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 4 * sizeof(float), nullptr);
	glEnableVertexAttribArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void draw() {
	glfwPollEvents();
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	model_shader.use();
	model_shader.setInt("matcap_sampler", 0);
	model_shader.setMat4("model", drag_rotation * Model);
	model_shader.setMat4("projection", Projection);
	model_shader.setMat4("view", View);
	//model_shader.setVec3("ViewPos", cam.position);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, tex_id);
	glBindVertexArray(mesh_vao);
	glDrawArrays(GL_TRIANGLES, 0, vertices.size());


	if (wired) {
		wire_shader.use();
		wire_shader.setMat4("model", drag_rotation * Model);
		wire_shader.setMat4("projection", Projection);
		wire_shader.setMat4("view", View);
		glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
		glBindVertexArray(mesh_vao);
		glDrawArrays(GL_TRIANGLES, 0, vertices.size());
		glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
	}
	if (anced) {
		anchor_shader.use();
		anchor_shader.setMat4("model", drag_rotation * Model);
		anchor_shader.setMat4("projection", Projection);
		anchor_shader.setMat4("view", View);
		glBindVertexArray(anchor_vao);
		int sum = 0;
		if(!select) for (auto t : anchors) sum += t.size();
		glDrawArrays(GL_POINTS, 0, sum);
	}
	glfwSwapBuffers(window);
}

void loop() {
	while (!glfwWindowShouldClose(window)) draw();
	glfwTerminate();
}

void geometry_to_origin() {
	float total_area = 0;
	glm::vec3 center(0);
	for (int i = 0; i < F.rows(); i++) {
		glm::vec3 p0, p1, p2;
		for (int j = 0; j < 3; j++) {
			p0[j] = V(F(i, 0), j);
			p1[j] = V(F(i, 1), j);
			p2[j] = V(F(i, 2), j);
		}
		glm::vec3 c = (p0 + p1 + p2) / 3.0f;
		float area = 0.5 * glm::length(glm::cross(p1 - p0, p2 - p0));
		center += c * area;
		total_area += area;
	}
	center /= total_area;
	for (int i = 0; i < V.rows(); i++) {
		V(i, 0) -= center[0];
		V(i, 1) -= center[1];
		V(i, 2) -= center[2];
	}
}

void init() {
	oV = V;
	n = V.rows();
	R.resize(n);
	for (int i = 0; i < F.rows(); i++) {
		for (int j = 0; j < 3; j++) {
			int cur = j, pre = (cur + 2) % 3, nxt = (cur + 1) % 3;
			H[F(i, cur)].insert(F(i, pre));
			H[F(i, cur)].insert(F(i, nxt));
			VF[F(i, j)].insert({ i, j });
		}
	}

	// bvh triangles
	triangles.resize(F.rows());
	
	vertices.resize(F.rows() * 9);
	normals.resize(F.rows() * 9);
	bind_data();
	update_bvh();
	update_mesh_buffer();
	update_normal_buffer();
	update_anchor_buffer();
	
	model_shader = Shader("./model_vs.txt", "./model_fs.txt");	
	anchor_shader = Shader("./anchor_vs.txt", "./anchor_fs.txt");
	wire_shader = Shader("./wire_vs.txt", "./wire_fs.txt");

	Projection = cam.projection();
	View = cam.view();
	Model = glm::mat4(1.0);
	drag_rotation = glm::mat4(1.0);

	glClearColor(0, 0, 0, 1);
	glEnable(GL_DEPTH_TEST);
	glPointSize(10);

	
}

int main(int argc, char *argv[]) {
	if (argc != 2) {
		cout << "./arap.exe \"path/to/mesh.obj\"" << endl;
		return 0;
	}
	if (glfw() == -1) {
		cout << "opengl error" << endl;
		return 0;
	}
	if (!igl::readOBJ(argv[1], V, F)) {
		cout << "read error" << endl;
		return 0;
	}

	geometry_to_origin();
	init();
	loop();
}