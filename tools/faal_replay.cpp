// Offline replay of a FAAL trace (faal_trace.csv).
// Rebuilds the filter exactly as it ran, then ablates each stage to show what
// actually shaped the output during the recorded session.
//
// usage: faal_replay <faal_trace.csv>
#include "faal_filter.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <vector>
#include <string>

struct Row { float dt, gx, gy, gz, signThr, freezeDur, revOn, smoothT; };

struct Header
{
	FaalParams p;
	bool ok = false;
};

static Header parseHeader(FILE* f)
{
	Header h;
	char line[512];
	long pos = ftell(f);
	while (fgets(line, sizeof line, f))
	{
		if (line[0] != '#') break;
		if (strstr(line, "rise=") == nullptr) continue;
		h.p.hystRiseTime = strtof(strstr(line, "rise=") + 5, nullptr);
		h.p.hystFallTime = strtof(strstr(line, "fall=") + 5, nullptr);
		h.p.peakDecayTime = strtof(strstr(line, "peakdecay=") + 10, nullptr);
		h.p.driftHoldTimeSec = strtof(strstr(line, "drift=") + 6, nullptr);
		h.p.oneEuroFcMin = strtof(strstr(line, "fcemin=") + 7, nullptr);
		h.p.oneEuroBeta = strtof(strstr(line, "beta=") + 5, nullptr);
		h.p.dirChangeMagRatio = strtof(strstr(line, "magratio=") + 9, nullptr);
		h.p.dirChangeAngleRad = strtof(strstr(line, "angle=") + 6, nullptr);
		h.p.dropSensitivity = strtof(strstr(line, "drop=") + 5, nullptr);
		h.p.dropCurve = strtof(strstr(line, "curve=") + 6, nullptr);
		h.p.peakMinTrigger = strtof(strstr(line, "peakmin=") + 8, nullptr);
		h.p.suppressThr = strtof(strstr(line, "suppressThr=") + 12, nullptr);
		h.p.freezeOutMul = strtof(strstr(line, "freezeMul=") + 10, nullptr);
		h.p.driftThreshold = strtof(strstr(line, "driftthr=") + 9, nullptr);
		h.p.enableDriftHold = strstr(line, "drift_on=1") != nullptr;
		h.ok = true;
		break;
	}
	fseek(f, pos, SEEK_SET); // re-read from after the '#' block
	while (fgets(line, sizeof line, f))
		if (line[0] != '#') break; // header row
	return h;
}

static std::vector<Row> load(const char* path, Header& h)
{
	std::vector<Row> rows;
	FILE* f = fopen(path, "rb");
	if (!f) { printf("cannot open %s\n", path); return rows; }
	h = parseHeader(f);
	char line[512];
	while (fgets(line, sizeof line, f))
	{
		if (line[0] == '#' || line[0] == 'd') continue;
		Row r;
		if (sscanf(line, "%f,%f,%f,%f,%f,%f,%f,%f",
		      &r.dt, &r.gx, &r.gy, &r.gz, &r.signThr, &r.freezeDur, &r.revOn, &r.smoothT) == 8)
			rows.push_back(r);
	}
	fclose(f);
	return rows;
}

static FaalParams paramsFor(const Header& h, const Row& r,
                            bool noSign, bool noRev, bool noDrop, bool noFreeze, bool noEuro)
{
	FaalParams p = h.p;
	p.signChangeThr = r.signThr;
	p.freezeDurationSec = r.freezeDur;
	p.enableVectorReversal = r.revOn;
	p.smoothTimeSec = r.smoothT;
	if (noSign) p.signChangeThr = 0.0f;
	if (noRev) p.enableVectorReversal = 0.0f;
	if (noDrop) p.dropSensitivity = 0.0f;
	if (noFreeze) p.freezeDurationSec = 0.0f;
	if (noEuro) { p.oneEuroFcMin = 1.0e6f; p.oneEuroBeta = 0.0f; }
	return p;
}

static std::vector<float> replay(const std::vector<Row>& rows, const Header& h,
                                 bool noSign, bool noRev, bool noDrop, bool noFreeze, bool noEuro)
{
	std::vector<float> out;
	out.reserve(rows.size());
	ResetFaalFilter();
	for (const Row& r : rows)
	{
		SetFaalParams(paramsFor(h, r, noSign, noRev, noDrop, noFreeze, noEuro));
		float x = r.gx, y = r.gy;
		faal_filter(x, y, r.dt);
		out.push_back(x);
	}
	return out;
}

static void report(const char* label, const std::vector<float>& out, const std::vector<float>& ref)
{
	double s = 0; float mx = 0; int n = 0, hit50 = 0, hit90 = 0;
	for (size_t i = 0; i < out.size(); ++i)
	{
		if (fabsf(ref[i]) < 1e-3f) continue;
		float sup = 1.0f - out[i] / ref[i];
		s += sup; n++;
		if (sup > 0.5f) hit50++;
		if (sup > 0.9f) hit90++;
		if (sup > mx) mx = sup;
	}
	if (n == 0) { printf("  %-28s (no motion)\n", label); return; }
	printf("  %-28s mean %5.1f%%   max %5.1f%%   >50%%: %5.1f%% of frames   >90%%: %5.1f%%\n",
	  label, 100.0 * s / n, mx * 100.0f, 100.0 * hit50 / n, 100.0 * hit90 / n);
}

int main(int argc, char** argv)
{
	if (argc < 2) { printf("usage: faal_replay <faal_trace.csv> | --selftest\n"); return 1; }

	// --selftest: synthesise a trace with the same format, then replay it.
	if (strcmp(argv[1], "--selftest") == 0)
	{
		FILE* f = fopen("faal_trace_selftest.csv", "wb");
		if (!f) { printf("cannot write selftest csv\n"); return 1; }
		fprintf(f, "# faal-trace v1\n");
		fprintf(f, "# rise=0.0029 fall=0.065 peakdecay=0.065 drift=0.2 fcemin=2 beta=0.0802 "
		           "magratio=0.15 angle=1.2 drop=2.4 curve=2 peakmin=2.2 suppressThr=0.45 "
		           "freezeMul=0.18 driftthr=0.15 drift_on=1\n");
		fprintf(f, "dt,gx,gy,gz,sign_thr,freeze_dur,rev_on,smooth_time\n");
		for (int i = 1; i <= 1500; ++i)
		{
			float t = i * 0.004f;
			float gx = 250.0f * sinf(2.0f * 3.14159265f * 4.0f * t) + 6.0f * sinf(2.0f * 3.14159265f * 35.0f * t);
			float gy = 40.0f * sinf(2.0f * 3.14159265f * 1.0f * t);
			fprintf(f, "0.004000,%.4f,%.4f,0.0000,0.5000,0.0240,1.0,0.007500\n", gx, gy);
		}
		fclose(f);
		argv[1] = (char*)"faal_trace_selftest.csv";
		printf("(selftest: synthetic trace written to %s)\n", argv[1]);
	}

	Header h;

	// --dump <csv> <startSec> <count>: raw input vs output for a window
	if (argc >= 5 && strcmp(argv[1], "--dump") == 0)
	{
		std::vector<Row> r2 = load(argv[2], h);
		if (r2.empty()) { printf("no rows\n"); return 1; }
		double t0 = atof(argv[3]); int cnt = atoi(argv[4]);
		auto f2 = replay(r2, h, false, false, false, false, false);
		auto ref2 = replay(r2, h, true, true, true, true, false);
		double t = 0; size_t start = 0;
		for (size_t i = 0; i < r2.size(); ++i) { if (t >= t0) { start = i; break; } t += r2[i].dt; }
		printf("  t(s)     dt(ms)   gx        gy      ref       out      sup%%\n");
		for (size_t i = start; i < start + cnt && i < r2.size(); ++i)
		{
			float sup = (fabsf(ref2[i]) < 1e-3f) ? 0.0f : 1.0f - f2[i] / ref2[i];
			printf("  %7.3f  %5.2f  %8.2f  %8.2f  %8.2f  %8.2f  %5.1f\n",
			  t, r2[i].dt * 1000, r2[i].gx, r2[i].gy, ref2[i], f2[i], sup * 100);
			t += r2[i].dt;
		}
		return 0;
	}

	std::vector<Row> rows = load(argv[1], h);
	if (rows.empty()) { printf("no rows\n"); return 1; }
	if (!h.ok) { printf("warning: no parameter header, using struct defaults\n"); }

	// --whatif <csv>: replay the same session under alternative tunings
	if (argc >= 3 && strcmp(argv[2], "--whatif") == 0)
	{
		auto refW = replay(rows, h, true, true, true, true, false);
		double rt = 0;
		for (size_t k = 0; k < rows.size(); ++k) rt += fabsf(refW[k]) * rows[k].dt;

		struct Case { const char* name; float thr; bool noRev; bool noSign; };
		Case cases[] = {
			{ "current (thr=0.5, rev on)",    -1.0f, false, false },
			{ "thr=2",                         2.0f, false, false },
			{ "thr=5",                         5.0f, false, false },
			{ "thr=10",                       10.0f, false, false },
			{ "thr=20",                       20.0f, false, false },
			{ "thr=10 + rev off",             10.0f, true,  false },
			{ "rev off only",                  -1.0f, true,  false },
			{ "sign off, rev on (ZL mode)",    -1.0f, false, true  },
			{ "sign+rev off (drop+freeze on)", -1.0f, true,  true  },
		};
		printf("\nwhat-if on this session (thr = FAAL_SIGN_CHANGE_THR):\n");
		printf("  %-30s %12s %12s\n", "config", "travel kept", "mean sup");
		for (auto& c : cases)
		{
			std::vector<float> out;
			out.reserve(rows.size());
			ResetFaalFilter();
			for (const Row& r : rows)
			{
				FaalParams p = paramsFor(h, r, c.noSign, c.noRev, false, false, false);
				if (c.thr >= 0.0f && !c.noSign) p.signChangeThr = c.thr;
				SetFaalParams(p);
				float x = r.gx, y = r.gy;
				faal_filter(x, y, r.dt);
				out.push_back(x);
			}
			double ot = 0, s = 0; size_t n = 0;
			for (size_t k = 0; k < rows.size(); ++k)
			{
				ot += fabsf(out[k]) * rows[k].dt;
				if (fabsf(refW[k]) >= 1.0f) { s += 1.0 - out[k] / refW[k]; n++; }
			}
			printf("  %-30s %11.1f%% %11.1f%%\n", c.name, 100.0 * ot / rt, n ? 100.0 * s / n : 0.0);
		}
		return 0;
	}


	double total = 0; float dtMin = 1e9f, dtMax = 0;
	for (auto& r : rows) { total += r.dt; if (r.dt < dtMin) dtMin = r.dt; if (r.dt > dtMax) dtMax = r.dt; }
	printf("frames: %zu   duration: %.2f s   mean rate: %.0f Hz   dt: %.1f..%.1f ms\n\n",
	  rows.size(), total, rows.size() / total, dtMin * 1000, dtMax * 1000);

	auto ref = replay(rows, h, true, true, true, true, false);   // no suppression at all
	auto full = replay(rows, h, false, false, false, false, false);
	auto noSign = replay(rows, h, true, false, false, false, false);
	auto noRev = replay(rows, h, false, true, false, false, false);
	auto noDrop = replay(rows, h, false, false, true, false, false);
	auto noFreeze = replay(rows, h, false, false, false, true, false);
	auto dropOnly = replay(rows, h, true, true, false, false, false);
	auto noEuro = replay(rows, h, false, false, false, false, true);

	printf("suppression of the recorded session:\n");
	report("full config", full, ref);
	report("- sign change (-SIGNCHG)", noSign, ref);
	report("- vector reversal (-REV)", noRev, ref);
	report("- drop (-DROP)", noDrop, ref);
	report("- freeze (-FREEZE)", noFreeze, ref);
	report("  drop only (no signchg/rev)", dropOnly, ref);
	printf("\n");
	report("full, one-euro disabled", noEuro, ref);

	// where does the brake actually fire?
	printf("\nbrake events (suppression > 60%%, 1 ms min), longest first:\n");
	struct Ev { size_t start, len; float peak; };
	std::vector<Ev> evs;
	size_t i = 0;
	while (i < full.size())
	{
		float sup = (fabsf(ref[i]) < 1e-3f) ? 0.0f : 1.0f - full[i] / ref[i];
		if (sup > 0.6f)
		{
			size_t j = i; float peak = 0;
			while (j < full.size())
			{
				float s2 = (fabsf(ref[j]) < 1e-3f) ? 0.0f : 1.0f - full[j] / ref[j];
				if (s2 <= 0.6f) break;
				if (s2 > peak) peak = s2;
				++j;
			}
			evs.push_back({ i, j - i, peak });
			i = j;
		}
		else ++i;
	}
	printf("  %zu events\n", evs.size());
	// top 10 by length
	for (int k = 0; k < 10 && k < (int)evs.size(); ++k)
	{
		size_t best = 0;
		for (size_t e = 1; e < evs.size(); ++e)
			if (evs[e].len > evs[best].len) best = e;
		double t0 = 0; for (size_t z = 0; z < evs[best].start; ++z) t0 += rows[z].dt;
		double dur = 0; for (size_t z = 0; z < evs[best].len; ++z) dur += rows[evs[best].start + z].dt;
		printf("  #%d t=%7.3f s  len=%5.1f ms  peak %5.1f%%\n", k + 1, t0, dur * 1000, evs[best].peak * 100);
		evs[best].len = 0;
	}

	// how much of the *motion* got suppressed, weighted by magnitude
	double wsum = 0, wcut = 0;
	for (size_t k = 0; k < full.size(); ++k)
	{
		float m = fabsf(ref[k]);
		wsum += m;
		wcut += m * ((fabsf(ref[k]) < 1e-3f) ? 0.0 : 1.0 - full[k] / ref[k]);
	}
	printf("\nmagnitude-weighted suppression: %.1f%% of all gyro travel\n", 100.0 * wcut / wsum);

	// ---- banded analysis: suppression vs. how fast the gyro was moving ----
	printf("\nsuppression by motion band (|reference output|):\n");
	printf("  %-14s %8s %9s %9s %9s %9s\n", "band deg/s", "frames", "share", "mean sup", ">50%", ">90%");
	const float edges[] = { 0.5f, 1.0f, 5.0f, 20.0f, 50.0f, 100.0f, 300.0f, 1.0e9f };
	for (int b = 0; b + 1 < (int)(sizeof(edges) / sizeof(edges[0])); ++b)
	{
		size_t n = 0, h50 = 0, h90 = 0;
		double s = 0;
		for (size_t k = 0; k < full.size(); ++k)
		{
			float m = fabsf(ref[k]);
			if (m < edges[b] || m >= edges[b + 1]) continue;
			float sup = 1.0f - full[k] / ref[k];
			if (sup < 0) sup = 0;
			n++; s += sup;
			if (sup > 0.5f) h50++;
			if (sup > 0.9f) h90++;
		}
		if (n == 0) continue;
		printf("  %6.1f-%-7.0f %8zu %8.1f%% %8.1f%% %8.1f%% %8.1f%%\n",
		  edges[b], edges[b + 1] > 1e8f ? 0.0f : edges[b + 1], n,
		  100.0 * n / full.size(), 100.0 * s / n,
		  100.0 * h50 / n, 100.0 * h90 / n);
	}

	// bit-exact fingerprint of the filter output — used to prove that a code
	// change did not alter behaviour on the same recording
	{
		unsigned long long h = 1469598103934665603ULL;
		for (size_t k = 0; k < full.size(); ++k)
		{
			unsigned int bits = 0;
			memcpy(&bits, &full[k], sizeof bits);
			h ^= bits; h *= 1099511628211ULL;
		}
		printf("\noutput fingerprint: %016llx\n", h);
	}

	// total angular travel kept
	{
		double rt = 0, ot = 0;
		for (size_t k = 0; k < rows.size(); ++k)
		{
			float dt = rows[k].dt;
			rt += fabsf(ref[k]) * dt;
			ot += fabsf(full[k]) * dt;
		}
		printf("\ntotal aim travel kept: %.1f%% (sum|out|*dt / sum|ref|*dt)\n", 100.0 * ot / rt);
	}

	// ---- mode split: chorded FAAL overrides ----
	size_t zl = 0, zr = 0, base = 0;
	double sZl = 0, sZr = 0, sBase = 0;
	size_t nZl = 0, nZr = 0, nBase = 0;
	for (size_t k = 0; k < rows.size(); ++k)
	{
		if (fabsf(ref[k]) < 1.0f) continue;
		float sup = 1.0f - full[k] / ref[k];
		if (sup < 0) sup = 0;
		if (rows[k].signThr == 0.0f && rows[k].revOn > 0.5f) { zl++; sZl += sup; nZl++; }
		else if (rows[k].signThr == 0.0f && rows[k].revOn <= 0.5f) { zr++; sZr += sup; nZr++; }
		else { base++; sBase += sup; nBase++; }
	}
	printf("\nmode split (frames with |motion| >= 1 deg/s):\n");
	printf("  default (signchg on)      %7zu frames  mean suppression %5.1f%%\n", base, nBase ? 100.0 * sBase / nBase : 0.0);
	printf("  ZL held (signchg off, rev on) %7zu frames  mean suppression %5.1f%%\n", zl, nZl ? 100.0 * sZl / nZl : 0.0);
	printf("  ZR held (all off)         %7zu frames  mean suppression %5.1f%%\n", zr, nZr ? 100.0 * sZr / nZr : 0.0);
	return 0;
}
