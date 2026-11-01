// Measured (not replayed) PS_MOTION gain, straight from the recorded output.
// usage: psmode_measure <csv>
#include <cstdio>
#include <cmath>
#include <cstring>
#include <vector>
#include <algorithm>

struct Row
{
	float dt, gx, gy, gz, signThr, freezeDur, revOn, smoothT;
	float rawX, rawY, rawZ, gX, gY, gZ, qW, qX, qY, qZ;
	float outX, outY, outZ;
};

static std::vector<Row> load(const char* p, int& cols)
{
	std::vector<Row> v; cols = 0;
	FILE* f = fopen(p, "rb");
	if (!f) { printf("cannot open %s\n", p); return v; }
	char line[1024];
	while (fgets(line, sizeof line, f))
	{
		if (line[0] == '#') continue;
		if (line[0] == 'd') { cols = 1 + (int)std::count(line, line + strlen(line), ','); continue; }
		Row r;
		if (sscanf(line,
		      "%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f",
		      &r.dt, &r.gx, &r.gy, &r.gz, &r.signThr, &r.freezeDur, &r.revOn, &r.smoothT,
		      &r.rawX, &r.rawY, &r.rawZ, &r.gX, &r.gY, &r.gZ, &r.qW, &r.qX, &r.qY, &r.qZ,
		      &r.outX, &r.outY, &r.outZ) == 21)
			v.push_back(r);
	}
	fclose(f);
	return v;
}

static void ps2d(float rx, float ry, float rz, float gx, float gy, float gz, float& yaw, float& pitch)
{
	float l = sqrtf(gx * gx + gy * gy + gz * gz);
	float ny = 0, nz = 0;
	if (l > 0) { ny = gy / l; nz = gz / l; }
	float w = ny * ry + nz * rz;
	float s = (w < 0.f) ? -1.f : 1.f;
	yaw = s * fminf(fabsf(w) * 2.f, sqrtf(ry * ry + rz * rz));
	pitch = -rx;
}

static float pct(std::vector<float> v, float p)
{
	if (v.empty()) return 0.f;
	std::sort(v.begin(), v.end());
	return v[(size_t)(p * (v.size() - 1))];
}

int main(int argc, char** argv)
{
	if (argc < 2) { printf("usage: psmode_measure <csv>\n"); return 1; }
	int cols = 0;
	std::vector<Row> rows = load(argv[1], cols);
	printf("%s\n  columns %d\n", argv[1], cols);
	if (rows.empty()) { printf("  needs the 21-column build (out_x/out_y/out_z)\n"); return 1; }

	std::vector<float> sY, sP, defl;
	double inTravel = 0, outTravel = 0, dur = 0;
	double maxResid = 0;

	for (auto& r : rows)
	{
		dur += r.dt;
		float sy = (fabsf(r.rawY) > 1e-3f) ? r.outY / r.rawY : 1.f;
		float sp = (fabsf(r.rawX) > 1e-3f) ? r.outX / r.rawX : 1.f;
		sY.push_back(sy); sP.push_back(sp);

		// forwarded vector re-projected into player space must equal out = s * in
		float oy, op;
		ps2d(r.outX, r.outY, r.outZ, r.gX, r.gY, r.gZ, oy, op);
		float ry = fabsf(oy - sy * r.gx);
		float rp = fabsf(op - sp * r.gy);
		if (ry > maxResid) maxResid = ry;
		if (rp > maxResid) maxResid = rp;

		float a = sqrtf(r.rawX * r.rawX + r.rawY * r.rawY + r.rawZ * r.rawZ);
		float b = sqrtf(r.outX * r.outX + r.outY * r.outY + r.outZ * r.outZ);
		if (a > 30.f && b > 1.f)
		{
			float c = (r.rawX * r.outX + r.rawY * r.outY + r.rawZ * r.outZ) / (a * b);
			c = c > 1.f ? 1.f : (c < -1.f ? -1.f : c);
			defl.push_back(acosf(c) * 57.2958f);
		}

		inTravel += sqrtf(r.gx * r.gx + r.gy * r.gy) * r.dt;
		outTravel += sqrtf(oy * oy + op * op) * r.dt;
	}

	printf("  duration %.1f s   frames %zu\n", dur, rows.size());
	printf("  s_yaw   p10 %.2f  p50 %.2f  p90 %.2f\n", pct(sY, .1f), pct(sY, .5f), pct(sY, .9f));
	printf("  s_pitch p10 %.2f  p50 %.2f  p90 %.2f\n", pct(sP, .1f), pct(sP, .5f), pct(sP, .9f));
	printf("  2D travel kept %.1f%%  (measured out/in)\n", 100.0 * outTravel / inTravel);
	printf("  3D direction rotated: p50 %.2f  p90 %.2f  p99 %.2f deg\n",
	  pct(defl, .5f), pct(defl, .9f), pct(defl, .99f));
	printf("  homogeneity residual (max |ps2d(out) - s*in|): %.2e\n", maxResid);
	return 0;
}
