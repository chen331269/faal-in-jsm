// Identifies which gyro space a v2 trace was recorded in, by recomputing the
// 2D transform offline from the raw 3D gyro + gravity and comparing with the
// values the app actually fed to FAAL.
//
// usage: space_check <faal_trace.csv>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <vector>
#include <algorithm>

struct Row
{
	float dt, gx, gy, gz, signThr, freezeDur, revOn, smoothT;
	float rawX, rawY, rawZ, gX, gY, gZ, qW, qX, qY, qZ;
};

static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

static std::vector<Row> load(const char* path)
{
	std::vector<Row> rows;
	FILE* f = fopen(path, "rb");
	if (!f) { printf("cannot open %s\n", path); return rows; }
	char line[1024];
	while (fgets(line, sizeof line, f))
	{
		if (line[0] == '#' || line[0] == 'd') continue;
		Row r;
		int n = sscanf(line,
		  "%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f,%f",
		  &r.dt, &r.gx, &r.gy, &r.gz, &r.signThr, &r.freezeDur, &r.revOn, &r.smoothT,
		  &r.rawX, &r.rawY, &r.rawZ, &r.gX, &r.gY, &r.gZ, &r.qW, &r.qX, &r.qY, &r.qZ);
		if (n == 18) rows.push_back(r);
	}
	fclose(f);
	return rows;
}

// JSM player space: gyroY = -rawGyroX, gyroX = sign*w*min(|w|*relax, |YZ|)
static void playerTurn(const Row& r, float& x, float& y, bool& clamped, float relax)
{
	float len = sqrtf(r.gX * r.gX + r.gY * r.gY + r.gZ * r.gZ);
	float nx = 0, ny = 0, nz = 0;
	if (len > 0) { nx = r.gX / len; ny = r.gY / len; nz = r.gZ / len; }
	float w = ny * r.rawY + nz * r.rawZ;
	float sign = w < 0.f ? -1.f : 1.f;
	float yz = sqrtf(r.rawY * r.rawY + r.rawZ * r.rawZ);
	float boosted = fabsf(w) * relax;
	clamped = boosted >= yz;
	x = sign * (boosted < yz ? boosted : yz);
	y = -r.rawX;
}

// JSM world turn: gyroX = G . V ; gyroY = -(pitchAxis . V) * sideReduction
static void worldTurn(const Row& r, float& x, float& y)
{
	float len = sqrtf(r.gX * r.gX + r.gY * r.gY + r.gZ * r.gZ);
	float nx = 0, ny = 0, nz = 0;
	if (len > 0) { nx = r.gX / len; ny = r.gY / len; nz = r.gZ / len; }
	float flat = fabsf(ny), up = fabsf(nz);
	float side = clampf((fmaxf(flat, up) - 0.125f) / 0.125f, 0.f, 1.f);

	x = nx * r.rawX + ny * r.rawY + nz * r.rawZ;

	float dot = nx;
	float px = 1.f - nx * dot, py = -ny * dot, pz = -nz * dot;
	float pl = px * px + py * py + pz * pz;
	if (pl > 0.f)
	{
		float inv = 1.f / sqrtf(pl);
		px *= inv; py *= inv; pz *= inv;
		y = -(px * r.rawX + py * r.rawY + pz * r.rawZ) * side;
	}
	else y = 0.f;
}

int main(int argc, char** argv)
{
	if (argc < 2) { printf("usage: space_check <csv>\n"); return 1; }
	std::vector<Row> rows = load(argv[1]);
	if (rows.empty()) { printf("no rows (need v2 trace)\n"); return 1; }

	double dur = 0; for (auto& r : rows) dur += r.dt;
	printf("frames %zu   duration %.1f s   mean %.0f Hz\n\n", rows.size(), dur, rows.size() / dur);

	double eP = 0, eW = 0, eL = 0, xP = 0, xW = 0, xL = 0;
	size_t clamped = 0, tilt = 0;
	double yawAbs = 0, pitAbs = 0;

	for (auto& r : rows)
	{
		float px, py, wx, wy;
		bool clamp;
		playerTurn(r, px, py, clamp, 2.0f);
		worldTurn(r, wx, wy);
		if (clamp) clamped++;

		float lx = r.rawX, ly = -r.rawY; // roughly LOCAL with default axis mask

		eP += fabs(px - r.gx) + fabs(py - r.gy);
		eW += fabs(wx - r.gx) + fabs(wy - r.gy);
		eL += fabs(lx - r.gx) + fabs(ly - r.gy);
		xP = fmaxf(xP, fmaxf(fabsf(px - r.gx), fabsf(py - r.gy)));
		xW = fmaxf(xW, fmaxf(fabsf(wx - r.gx), fabsf(wy - r.gy)));
		xL = fmaxf(xL, fmaxf(fabsf(lx - r.gx), fabsf(ly - r.gy)));

		float gl = sqrtf(r.gX * r.gX + r.gY * r.gY + r.gZ * r.gZ);
		if (gl > 0) { float ny = r.gY / gl, nz = r.gZ / gl; if (fmaxf(fabsf(ny), fabsf(nz)) < 0.5f) tilt++; }
		yawAbs += fabs(r.gx);
		pitAbs += fabs(r.gy);
	}

	const double n = (double)rows.size();
	printf("match against the recorded 2D values (mean |err| per axis, max):\n");
	printf("  PLAYER_TURN  mean %8.3f  max %8.3f\n", eP / (2 * n), xP);
	printf("  WORLD_TURN   mean %8.3f  max %8.3f\n", eW / (2 * n), xW);
	printf("  LOCAL        mean %8.3f  max %8.3f\n", eL / (2 * n), xL);

	printf("\nplayer-space details:\n");
	printf("  relaxFactor clamp active in %zu / %zu frames (%.1f%%)\n", clamped, rows.size(), 100.0 * clamped / n);
	printf("  gravity far from both axes (tilted >30deg): %.1f%% of frames\n", 100.0 * tilt / n);
	printf("  mean |yaw| %.1f deg/s   mean |pitch| %.1f deg/s   ratio %.2f\n",
	  yawAbs / n, pitAbs / n, (pitAbs > 0 ? yawAbs / pitAbs : 0));

	// Per-frame disagreement between the two space definitions, on the SAME data.
	std::vector<float> dYaw, dPitch, dVec;
	for (auto& r : rows)
	{
		float px, py, wx, wy;
		bool c;
		playerTurn(r, px, py, c, 2.0f);
		worldTurn(r, wx, wy);
		dYaw.push_back(fabsf(px - wx));
		dPitch.push_back(fabsf(py - wy));
		// angle between the 2D motion vectors, in degrees
		float a = sqrtf(px * px + py * py), b = sqrtf(wx * wx + wy * wy);
		if (a > 1.f && b > 1.f)
		{
			float cosv = (px * wx + py * wy) / (a * b);
			cosv = cosv > 1.f ? 1.f : (cosv < -1.f ? -1.f : cosv);
			dVec.push_back(acosf(cosv) * 57.2958f);
		}
	}
	auto pct = [](std::vector<float> v, float p)
	{
		if (v.empty()) return 0.f;
		std::sort(v.begin(), v.end());
		size_t i = (size_t)(p * (v.size() - 1));
		return v[i];
	};
	printf("\nspace disagreement on the same frames (PLAYER vs WORLD):\n");
	printf("  |dYaw|   p50 %6.2f   p90 %6.2f   p99 %6.2f deg/s\n",
	  pct(dYaw, 0.50f), pct(dYaw, 0.90f), pct(dYaw, 0.99f));
	printf("  |dPitch| p50 %6.2f   p90 %6.2f   p99 %6.2f deg/s\n",
	  pct(dPitch, 0.50f), pct(dPitch, 0.90f), pct(dPitch, 0.99f));
	if (!dVec.empty())
		printf("  2D vector angle: p50 %5.1f   p90 %5.1f   p99 %5.1f deg (n=%zu)\n",
		  pct(dVec, 0.50f), pct(dVec, 0.90f), pct(dVec, 0.99f), dVec.size());
	double over30 = 0;
	for (size_t i = 0; i < dVec.size(); ++i) if (dVec[i] > 30.f) over30++;
	if (!dVec.empty()) printf("  frames where the two definitions point >30deg apart: %.1f%%\n", 100.0 * over30 / dVec.size());

	// How much does the grip actually rotate during the session? Measured as the
	// angle between each frame's gravity direction and the session's mean gravity.
	{
		float mx = 0, my = 0, mz = 0;
		std::vector<float> gx, gy, gz;
		for (auto& r : rows)
		{
			float l = sqrtf(r.gX * r.gX + r.gY * r.gY + r.gZ * r.gZ);
			if (l < 1e-3f) continue;
			gx.push_back(r.gX / l); gy.push_back(r.gY / l); gz.push_back(r.gZ / l);
			mx += r.gX / l; my += r.gY / l; mz += r.gZ / l;
		}
		if (!gx.empty())
		{
			float ml = sqrtf(mx * mx + my * my + mz * mz);
			if (ml > 0) { mx /= ml; my /= ml; mz /= ml; }
			std::vector<float> ang;
			ang.reserve(gx.size());
			for (size_t i = 0; i < gx.size(); ++i)
			{
				float c = gx[i] * mx + gy[i] * my + gz[i] * mz;
				c = c > 1.f ? 1.f : (c < -1.f ? -1.f : c);
				ang.push_back(acosf(c) * 57.2958f);
			}
			printf("\ngrip rotation during the session (angle from mean gravity):\n");
			printf("  p50 %5.1f   p90 %5.1f   p99 %5.1f   max %5.1f deg\n",
			  pct(ang, 0.50f), pct(ang, 0.90f), pct(ang, 0.99f), pct(ang, 1.0f));
			size_t a10 = 0, a30 = 0;
			for (float a : ang) { if (a > 10.f) a10++; if (a > 30.f) a30++; }
			printf("  frames beyond 10deg: %.1f%%   beyond 30deg: %.1f%%\n",
			  100.0 * a10 / ang.size(), 100.0 * a30 / ang.size());
		}
	}
	// Homogeneity: the pre-filter relies on "scaling (Y,Z) scales the delivered
	// yaw by the same factor". Verify that on real data instead of trusting it.
	{
		double maxY = 0, maxP = 0;
		for (auto& r : rows)
		{
			bool c;
			float y0, p0, y1, p1, y2, p2;
			playerTurn(r, y0, p0, c, 2.0f);

			Row sy = r; sy.rawY *= 0.5f; sy.rawZ *= 0.5f;
			playerTurn(sy, y1, p1, c, 2.0f);
			if (fabsf(y0) > 1.f)
				maxY = fmax(maxY, fabsf(y1 - 0.5f * y0) / fabsf(y0));

			Row sp = r; sp.rawX *= 0.3f;
			playerTurn(sp, y2, p2, c, 2.0f);
			if (fabsf(p0) > 1.f)
				maxP = fmax(maxP, fabsf(p2 - 0.3f * p0) / fabsf(p0));
		}
		printf("\nhomogeneity check (scale 0.5 on YZ / 0.3 on X):\n");
		printf("  max relative error  yaw %.2e   pitch %.2e\n", maxY, maxP);
	}

	return 0;
}
