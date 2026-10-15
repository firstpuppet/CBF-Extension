/*
unit tests for src/core/timing.hpp. no dependencies, build with tests/CMakeLists.txt:
  cmake -S tests -B build-tests && cmake --build build-tests && ctest --test-dir build-tests --output-on-failure
*/

#include "core/timing.hpp"

#include <cmath>
#include <cstdio>
#include <functional>
#include <vector>

using namespace cbf::timing;

// ---- minimal test runner ----

namespace {

int checks = 0;
int failures = 0;

std::vector<std::pair<const char*, std::function<void()>>>& registry() {
	static std::vector<std::pair<const char*, std::function<void()>>> tests;
	return tests;
}

struct Register {
	Register(const char* name, std::function<void()> fn) { registry().emplace_back(name, std::move(fn)); }
};

}

#define TEST(name) \
	static void name(); \
	static Register register_##name(#name, name); \
	static void name()

#define CHECK(cond) do { \
	checks++; \
	if (!(cond)) { failures++; std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } \
} while (0)

#define CHECK_NEAR(a, b) CHECK(std::abs((a) - (b)) < 1e-9)

// ---- helpers ----

// 4 steps per frame over 1 second, so each step is 0.25s long
StepPlan plan4(std::vector<double> timestamps) {
	return planSteps(timestamps, 0.0, 1.0, 4);
}

int endSteps(StepPlan const& plan) {
	int count = 0;
	for (auto const& step : plan.steps) count += step.endStep();
	return count;
}

// ---- planSteps ----

TEST(no_inputs_gives_whole_steps) {
	auto plan = plan4({});
	CHECK(plan.steps.size() == 4);
	CHECK(plan.inputsUsed == 0);
	for (auto const& step : plan.steps) {
		CHECK(step.endStep());
		CHECK_NEAR(step.deltaFactor, 1.0);
	}
}

TEST(input_splits_its_step) {
	auto plan = plan4({ 0.30 }); // 20% into step 1
	CHECK(plan.steps.size() == 5);
	CHECK(plan.inputsUsed == 1);

	CHECK(plan.steps[0].endStep()); // step 0 untouched
	CHECK(plan.steps[1].input == 0);
	CHECK_NEAR(plan.steps[1].deltaFactor, 0.2);
	CHECK(plan.steps[2].endStep());
	CHECK_NEAR(plan.steps[2].deltaFactor, 0.8);
}

TEST(two_inputs_on_one_step) {
	auto plan = plan4({ 0.30, 0.40 });
	CHECK(plan.inputsUsed == 2);
	CHECK(plan.steps[1].input == 0);
	CHECK_NEAR(plan.steps[1].deltaFactor, 0.2);
	CHECK(plan.steps[2].input == 1);
	CHECK_NEAR(plan.steps[2].deltaFactor, 0.4); // from 20% to 60%
	CHECK(plan.steps[3].endStep());
	CHECK_NEAR(plan.steps[3].deltaFactor, 0.4);
}

TEST(simultaneous_inputs_get_smallest_step) {
	auto plan = plan4({ 0.30, 0.30 });
	CHECK(plan.steps[2].input == 1);
	CHECK(plan.steps[2].deltaFactor == SMALLEST_FLOAT); // never 0
}

TEST(input_at_frame_start) {
	auto plan = plan4({ 0.0 });
	CHECK(plan.steps[0].input == 0);
	CHECK(plan.steps[0].deltaFactor == SMALLEST_FLOAT);
	CHECK_NEAR(plan.steps[1].deltaFactor, 1.0);
}

TEST(input_on_step_boundary_goes_to_next_step) {
	auto plan = plan4({ 0.25 });
	CHECK(plan.steps[0].endStep());
	CHECK(plan.steps[1].input == 0);
	CHECK(plan.steps[1].deltaFactor == SMALLEST_FLOAT);
}

TEST(inputs_after_frame_are_kept) {
	auto plan = plan4({ 0.10, 1.50 });
	CHECK(plan.inputsUsed == 1); // second input is for the next frame
	CHECK(endSteps(plan) == 4);
}

TEST(input_in_last_step) {
	auto plan = plan4({ 0.90 });
	CHECK(plan.steps.size() == 5);
	CHECK(plan.steps[3].input == 0);
	CHECK_NEAR(plan.steps[3].deltaFactor, 0.6);
	CHECK_NEAR(plan.steps[4].deltaFactor, 0.4);
}

TEST(zero_steps_gives_empty_plan) {
	std::vector<double> timestamps = { 0.5 };
	auto plan = planSteps(timestamps, 0.0, 1.0, 0);
	CHECK(plan.steps.empty());
	CHECK(plan.inputsUsed == 0);
}

TEST(every_step_adds_up_to_one) {
	std::vector<double> timestamps;
	for (int i = 0; i < 40; i++) timestamps.push_back(0.013 + i * 0.0247);
	auto plan = planSteps(timestamps, 0.0, 1.0, 7);

	CHECK(endSteps(plan) == 7);
	double sum = 0.0;
	for (auto const& step : plan.steps) {
		CHECK(step.deltaFactor > 0.0);
		CHECK(step.deltaFactor <= 1.0);
		sum += step.deltaFactor;
		if (step.endStep()) {
			CHECK(std::abs(sum - 1.0) < 1e-6);
			sum = 0.0;
		}
	}
}

TEST(frame_offset_doesnt_matter) {
	// same frame as input_splits_its_step, shifted to a realistic timestamp
	std::vector<double> timestamps = { 1000.0 + 0.30 / 60.0 };
	auto plan = planSteps(timestamps, 1000.0, 1000.0 + 1.0 / 60.0, 4);
	CHECK(plan.steps[1].input == 0);
	CHECK(std::abs(plan.steps[1].deltaFactor - 0.2) < 1e-6);
}

// ---- stepCount ----

int count(double delta, float timewarp, StepMode mode, double animationInterval = 1.0 / 60.0) {
	double averageDelta = delta;
	return stepCount(delta, timewarp, mode, animationInterval, averageDelta);
}

TEST(vanilla_step_count) {
	CHECK(count(1.0 / 60.0, 1.0f, StepMode::Vanilla) == 4);
	CHECK(count(1.0 / 240.0, 1.0f, StepMode::Vanilla) == 1);
	CHECK(count(1.0 / 1000.0, 1.0f, StepMode::Vanilla) == 1); // at least 1
	CHECK(count(1.0 / 60.0, 0.5f, StepMode::Vanilla) == 8); // slower timewarp needs more steps
	CHECK(count(1.0 / 60.0, 2.0f, StepMode::Vanilla) == 4); // faster timewarp doesn't
}

TEST(legacy_step_count) {
	CHECK(count(1.0 / 60.0, 1.0f, StepMode::Legacy) == 4);
	CHECK(count(1.0 / 240.0, 1.0f, StepMode::Legacy) == 4); // 2.1: at least 4 per frame
	CHECK(count(1.0 / 30.0, 1.0f, StepMode::Legacy) == 8);
	CHECK(count(1.0 / 240.0, 0.5f, StepMode::Legacy) == 8);
}

TEST(bypass_step_count_steady) {
	CHECK(count(1.0 / 144.0, 1.0f, StepMode::Bypass, 1.0 / 144.0) == 2); // 1.67 steps -> 2
	CHECK(count(1.0 / 360.0, 1.0f, StepMode::Bypass, 1.0 / 360.0) == 1);
	CHECK(count(1.0 / 240.0, 1.0f, StepMode::Bypass, 1.0 / 240.0) == 1); // exactly 1, not 2
	CHECK(count(1.0 / 60.0, 1.0f, StepMode::Bypass, 1.0 / 60.0) == 4);
}

TEST(bypass_step_count_lagging) {
	CHECK(count(0.05, 1.0f, StepMode::Bypass, 1.0 / 240.0) == 12); // big lag spike: catch up fully
	CHECK(count(0.0055, 1.0f, StepMode::Bypass, 1.0 / 240.0) == 2); // consistently slow: use the average
}

TEST(bypass_average_is_capped) {
	double averageDelta = 0.0;
	stepCount(10.0, 1.0f, StepMode::Bypass, 0.01, averageDelta);
	CHECK_NEAR(averageDelta, 0.1); // 10x the animation interval
}

TEST(bypass_average_smooths) {
	double averageDelta = 0.01;
	stepCount(0.02, 1.0f, StepMode::Bypass, 0.01, averageDelta);
	CHECK_NEAR(averageDelta, 0.0105); // 5% of the new delta
}

// ---- usesLegacyPhysics ----

TEST(legacy_physics_detection) {
	CHECK(usesLegacyPhysics(21, 0)); // 2.1 level
	CHECK(usesLegacyPhysics(19, 0)); // 1.9 level
	CHECK(!usesLegacyPhysics(22, 0)); // 2.2 level
	CHECK(!usesLegacyPhysics(21, 1234)); // verified in 2.2
	CHECK(!usesLegacyPhysics(0, 0)); // local/unknown
}

// ---- main ----

int main() {
	for (auto const& [name, fn] : registry()) {
		int before = failures;
		fn();
		std::printf("%s %s\n", failures == before ? "ok  " : "FAIL", name);
	}

	std::printf("\n%d checks, %d failed\n", checks, failures);
	return failures == 0 ? 0 : 1;
}
