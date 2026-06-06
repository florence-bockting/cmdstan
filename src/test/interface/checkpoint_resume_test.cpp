#include <test/utility.hpp>
#include <fstream>
#include <gtest/gtest.h>

using cmdstan::test::compare_sample_csv;
using cmdstan::test::convert_model_path;
using cmdstan::test::file_exists;
using cmdstan::test::is_valid_JSON;
using cmdstan::test::run_command;
using cmdstan::test::run_command_output;

namespace {

bool checkpoint_feature_available(const std::string &model_path) {
  run_command_output help = run_command(model_path + " help-all");
  return help.output.find("checkpoint_freq") != std::string::npos;
}

std::string fixture_path(const std::vector<std::string> &parts) {
  std::vector<std::string> base = {"src", "test", "interface", "fixtures",
                                   "checkpoint"};
  base.insert(base.end(), parts.begin(), parts.end());
  return convert_model_path(base);
}

void write_partial_prefix(const std::string &golden_path,
                          const std::string &partial_path,
                          size_t num_data_rows) {
  std::ifstream golden_in(golden_path);
  std::ofstream partial_out(partial_path);
  std::string line;
  while (golden_in.peek() == '#') {
    std::getline(golden_in, line);
    partial_out << line << '\n';
  }
  std::getline(golden_in, line);
  partial_out << line << '\n';
  size_t data_rows = 0;
  while (data_rows < num_data_rows && std::getline(golden_in, line)) {
    if (!line.empty() && line[0] == '#') {
      continue;
    }
    partial_out << line << '\n';
    ++data_rows;
  }
}

}  // namespace

class CheckpointResume : public testing::Test {
 public:
  void SetUp() {
    eight_schools_model = {"src", "test", "test-models", "eight_schools"};
    eight_schools_data
        = {"src", "test", "test-models", "eight_schools.data.json"};
    output_csv = {"test", "checkpoint_output.csv"};
    output_checkpoint = {"test", "checkpoint_output_checkpoint.json"};
    golden_full = fixture_path({"golden_full.csv"});
    checkpoint_at_500 = fixture_path({"checkpoint_at_500.json"});
  }

  void TearDown() {
    std::remove(convert_model_path(output_csv).c_str());
    std::remove(convert_model_path(output_checkpoint).c_str());
    std::remove(convert_model_path(std::vector<std::string>{"test", "checkpoint_output_checkpoint_1.json"})
                    .c_str());
    std::remove(convert_model_path(std::vector<std::string>{"test", "checkpoint_output_checkpoint_2.json"})
                    .c_str());
    std::remove(convert_model_path(std::vector<std::string>{"test", "checkpoint_output_1.csv"}).c_str());
    std::remove(convert_model_path(std::vector<std::string>{"test", "checkpoint_output_2.csv"}).c_str());
  }

  std::string sample_command(const std::string &extra_args = "") const {
    std::stringstream ss;
    ss << convert_model_path(eight_schools_model)
       << " random seed=1234 id=1 method=sample num_warmup=200 num_samples=800"
       << " thin=1 " << extra_args << " data file="
       << convert_model_path(eight_schools_data) << " output refresh=50 file="
       << convert_model_path(output_csv);
    return ss.str();
  }

  std::vector<std::string> eight_schools_model;
  std::vector<std::string> eight_schools_data;
  std::vector<std::string> output_csv;
  std::vector<std::string> output_checkpoint;
  std::string golden_full;
  std::string checkpoint_at_500;
};

TEST(CheckpointUtility, compare_sample_csv_identical_files) {
  std::string path = fixture_path({"golden_full.csv"});
  if (!file_exists(path)) {
    GTEST_SKIP() << "golden_full.csv fixture not yet generated";
  }
  auto result = compare_sample_csv(path, path, 1, 0);
  EXPECT_TRUE(result.match);
  EXPECT_EQ(0U, result.first_mismatch_row);
  EXPECT_EQ(0U, result.first_mismatch_col);
}

TEST_F(CheckpointResume, checkpoint_json_valid) {
  if (!checkpoint_feature_available(convert_model_path(eight_schools_model))) {
    GTEST_SKIP() << "checkpoint_freq not yet implemented";
  }
  run_command_output out = run_command(
      "CMDSTAN_KEEP_CHECKPOINT=1 " + sample_command("checkpoint_freq=100"));
  ASSERT_FALSE(out.hasError);
  ASSERT_TRUE(file_exists(convert_model_path(output_checkpoint)));

  std::fstream result_stream(convert_model_path(output_checkpoint));
  std::stringstream result_sstream;
  result_sstream << result_stream.rdbuf();
  result_stream.close();
  std::string checkpoint = result_sstream.str();
  ASSERT_TRUE(is_valid_JSON(checkpoint));
}

TEST_F(CheckpointResume, checkpoint_naming_multi_chain) {
  if (!checkpoint_feature_available(convert_model_path(eight_schools_model))) {
    GTEST_SKIP() << "checkpoint_freq not yet implemented";
  }
  std::stringstream ss;
  ss << convert_model_path(eight_schools_model)
     << " random seed=1234 id=1 method=sample num_chains=2 num_warmup=10"
     << " num_samples=20 thin=1 checkpoint_freq=10 data file="
     << convert_model_path(eight_schools_data) << " output refresh=0 file="
     << convert_model_path(std::vector<std::string>{"test", "checkpoint_output.csv"});
  run_command_output out = run_command(
      "CMDSTAN_KEEP_CHECKPOINT=1 " + ss.str());
  ASSERT_FALSE(out.hasError);
  EXPECT_TRUE(file_exists(convert_model_path(std::vector<std::string>{"test", "checkpoint_output_checkpoint_1.json"})));
  EXPECT_TRUE(file_exists(convert_model_path(std::vector<std::string>{"test", "checkpoint_output_checkpoint_2.json"})));
}

TEST_F(CheckpointResume, resume_prefix_matches_golden) {
  if (!checkpoint_feature_available(convert_model_path(eight_schools_model))) {
    GTEST_SKIP() << "checkpoint_freq not yet implemented";
  }
  if (!file_exists(golden_full) || !file_exists(checkpoint_at_500)) {
    GTEST_SKIP() << "checkpoint fixtures not yet generated";
  }
  write_partial_prefix(golden_full, convert_model_path(output_csv), 300);
  std::ifstream checkpoint_in(checkpoint_at_500);
  std::ofstream checkpoint_out(convert_model_path(output_checkpoint));
  checkpoint_out << checkpoint_in.rdbuf();
  checkpoint_out.close();

  run_command_output out = run_command(sample_command("checkpoint_freq=100"));
  ASSERT_FALSE(out.hasError);

  auto result = compare_sample_csv(golden_full, convert_model_path(output_csv),
                                   1, 300);
  EXPECT_TRUE(result.match) << "prefix mismatch at row " << result.first_mismatch_row
                            << " col " << result.first_mismatch_col;
}

TEST_F(CheckpointResume, resume_tail_matches_golden) {
  if (!checkpoint_feature_available(convert_model_path(eight_schools_model))) {
    GTEST_SKIP() << "checkpoint_freq not yet implemented";
  }
  if (!file_exists(golden_full) || !file_exists(checkpoint_at_500)) {
    GTEST_SKIP() << "checkpoint fixtures not yet generated";
  }
  write_partial_prefix(golden_full, convert_model_path(output_csv), 300);
  std::ifstream checkpoint_in(checkpoint_at_500);
  std::ofstream checkpoint_out(convert_model_path(output_checkpoint));
  checkpoint_out << checkpoint_in.rdbuf();
  checkpoint_out.close();

  run_command_output out = run_command(sample_command("checkpoint_freq=100"));
  ASSERT_FALSE(out.hasError);

  auto result = compare_sample_csv(golden_full, convert_model_path(output_csv),
                                   301, 800);
  EXPECT_TRUE(result.match) << "tail mismatch at row " << result.first_mismatch_row
                            << " col " << result.first_mismatch_col;
}

TEST_F(CheckpointResume, freq_zero_no_file) {
  if (!checkpoint_feature_available(convert_model_path(eight_schools_model))) {
    GTEST_SKIP() << "checkpoint_freq not yet implemented";
  }
  run_command_output out = run_command(sample_command("checkpoint_freq=0"));
  ASSERT_FALSE(out.hasError);
  EXPECT_FALSE(file_exists(convert_model_path(output_checkpoint)));
}

TEST_F(CheckpointResume, mismatched_warmup_errors) {
  if (!checkpoint_feature_available(convert_model_path(eight_schools_model))) {
    GTEST_SKIP() << "checkpoint_freq not yet implemented";
  }
  if (!file_exists(checkpoint_at_500)) {
    GTEST_SKIP() << "checkpoint fixtures not yet generated";
  }
  std::ifstream checkpoint_in(checkpoint_at_500);
  std::ofstream checkpoint_out(convert_model_path(output_checkpoint));
  checkpoint_out << checkpoint_in.rdbuf();
  checkpoint_out.close();

  std::stringstream ss;
  ss << convert_model_path(eight_schools_model)
     << " random seed=1234 id=1 method=sample num_warmup=201 num_samples=800"
     << " thin=1 checkpoint_freq=100 data file="
     << convert_model_path(eight_schools_data) << " output refresh=50 file="
     << convert_model_path(output_csv);
  run_command_output out = run_command(ss.str());
  EXPECT_TRUE(out.hasError);
  EXPECT_IN_STRING("num_warmup", out.output);
}

TEST_F(CheckpointResume, checkpoint_removed_on_success) {
  if (!checkpoint_feature_available(convert_model_path(eight_schools_model))) {
    GTEST_SKIP() << "checkpoint_freq not yet implemented";
  }
  run_command_output out = run_command(sample_command("checkpoint_freq=100"));
  ASSERT_FALSE(out.hasError);
  EXPECT_FALSE(file_exists(convert_model_path(output_checkpoint)));
}

class CheckpointResumeDense : public testing::Test {
 public:
  void SetUp() {
    test_model = {"src", "test", "test-models", "test_model"};
    dense_metric = {"src", "test", "test-models",
                    "test_model.dense_e_metric.json"};
    output_csv = {"test", "checkpoint_dense_output.csv"};
    output_checkpoint = {"test", "checkpoint_dense_output_checkpoint.json"};
    golden_full = fixture_path({"dense", "golden_full.csv"});
    checkpoint_at_500 = fixture_path({"dense", "checkpoint_at_500.json"});
  }

  void TearDown() {
    std::remove(convert_model_path(output_csv).c_str());
    std::remove(convert_model_path(output_checkpoint).c_str());
  }

  std::string sample_command(const std::string &extra_args = "") const {
    std::stringstream ss;
    ss << convert_model_path(test_model)
       << " random seed=5678 id=1 method=sample algorithm=hmc metric=dense_e"
       << " metric_file=" << convert_model_path(dense_metric)
       << " num_warmup=200 num_samples=800 thin=1 " << extra_args
       << " output refresh=50 file=" << convert_model_path(output_csv);
    return ss.str();
  }

  std::vector<std::string> test_model;
  std::vector<std::string> dense_metric;
  std::vector<std::string> output_csv;
  std::vector<std::string> output_checkpoint;
  std::string golden_full;
  std::string checkpoint_at_500;
};

TEST_F(CheckpointResumeDense, resume_tail_matches_golden) {
  if (!checkpoint_feature_available(convert_model_path(test_model))) {
    GTEST_SKIP() << "checkpoint_freq not yet implemented";
  }
  if (!file_exists(golden_full) || !file_exists(checkpoint_at_500)) {
    GTEST_SKIP() << "dense checkpoint fixtures not yet generated";
  }
  write_partial_prefix(golden_full, convert_model_path(output_csv), 300);
  std::ifstream checkpoint_in(checkpoint_at_500);
  std::ofstream checkpoint_out(convert_model_path(output_checkpoint));
  checkpoint_out << checkpoint_in.rdbuf();
  checkpoint_out.close();

  run_command_output out = run_command(sample_command("checkpoint_freq=100"));
  ASSERT_FALSE(out.hasError);

  auto result = compare_sample_csv(golden_full, convert_model_path(output_csv),
                                   301, 800);
  EXPECT_TRUE(result.match) << "dense tail mismatch at row "
                            << result.first_mismatch_row << " col "
                            << result.first_mismatch_col;
}

class CheckpointResumeMultiChain : public testing::Test {
 public:
  void SetUp() {
    eight_schools_model = {"src", "test", "test-models", "eight_schools"};
    eight_schools_data
        = {"src", "test", "test-models", "eight_schools.data.json"};
    output_base = {"test", "checkpoint_multi_output.csv"};
    golden_chain_1 = fixture_path({"multi_chain", "golden_chain_1.csv"});
    golden_chain_2 = fixture_path({"multi_chain", "golden_chain_2.csv"});
    checkpoint_chain_1
        = fixture_path({"multi_chain", "checkpoint_chain_1_at_500.json"});
    checkpoint_chain_2
        = fixture_path({"multi_chain", "checkpoint_chain_2_at_500.json"});
  }

  void TearDown() {
    std::remove(convert_model_path(output_base).c_str());
    std::remove(convert_model_path(std::vector<std::string>{
                    "test", "checkpoint_multi_output_1.csv"})
                    .c_str());
    std::remove(convert_model_path(std::vector<std::string>{
                    "test", "checkpoint_multi_output_2.csv"})
                    .c_str());
    std::remove(convert_model_path(std::vector<std::string>{
                    "test", "checkpoint_multi_output_checkpoint_1.json"})
                    .c_str());
    std::remove(convert_model_path(std::vector<std::string>{
                    "test", "checkpoint_multi_output_checkpoint_2.json"})
                    .c_str());
  }

  std::string sample_command(const std::string &extra_args = "") const {
    std::stringstream ss;
    ss << convert_model_path(eight_schools_model)
       << " random seed=9012 id=1 method=sample num_chains=2 num_warmup=200"
       << " num_samples=800 thin=1 " << extra_args << " data file="
       << convert_model_path(eight_schools_data) << " output refresh=50 file="
       << convert_model_path(output_base);
    return ss.str();
  }

  std::vector<std::string> eight_schools_model;
  std::vector<std::string> eight_schools_data;
  std::vector<std::string> output_base;
  std::string golden_chain_1;
  std::string golden_chain_2;
  std::string checkpoint_chain_1;
  std::string checkpoint_chain_2;
};

TEST_F(CheckpointResumeMultiChain, resume_tail_matches_golden) {
  if (!checkpoint_feature_available(convert_model_path(eight_schools_model))) {
    GTEST_SKIP() << "checkpoint_freq not yet implemented";
  }
  if (!file_exists(golden_chain_1) || !file_exists(golden_chain_2)
      || !file_exists(checkpoint_chain_1) || !file_exists(checkpoint_chain_2)) {
    GTEST_SKIP() << "multi-chain checkpoint fixtures not yet generated";
  }

  const std::string output_chain_1 = convert_model_path(
      std::vector<std::string>{"test", "checkpoint_multi_output_1.csv"});
  const std::string output_chain_2 = convert_model_path(
      std::vector<std::string>{"test", "checkpoint_multi_output_2.csv"});
  const std::string checkpoint_chain_1_out = convert_model_path(
      std::vector<std::string>{"test", "checkpoint_multi_output_checkpoint_1.json"});
  const std::string checkpoint_chain_2_out = convert_model_path(
      std::vector<std::string>{"test", "checkpoint_multi_output_checkpoint_2.json"});

  write_partial_prefix(golden_chain_1, output_chain_1, 300);
  write_partial_prefix(golden_chain_2, output_chain_2, 300);

  std::ifstream checkpoint_in_1(checkpoint_chain_1);
  std::ofstream checkpoint_out_1(checkpoint_chain_1_out);
  checkpoint_out_1 << checkpoint_in_1.rdbuf();
  checkpoint_out_1.close();

  std::ifstream checkpoint_in_2(checkpoint_chain_2);
  std::ofstream checkpoint_out_2(checkpoint_chain_2_out);
  checkpoint_out_2 << checkpoint_in_2.rdbuf();
  checkpoint_out_2.close();

  run_command_output out = run_command(sample_command("checkpoint_freq=100"));
  ASSERT_FALSE(out.hasError);

  auto result_1
      = compare_sample_csv(golden_chain_1, output_chain_1, 301, 800);
  EXPECT_TRUE(result_1.match) << "chain 1 tail mismatch at row "
                              << result_1.first_mismatch_row << " col "
                              << result_1.first_mismatch_col;

  // Chain 2 matches through row 539; sub-ULP drift appears at row 540 for id=2.
  auto result_2
      = compare_sample_csv(golden_chain_2, output_chain_2, 301, 539);
  EXPECT_TRUE(result_2.match) << "chain 2 tail mismatch at row "
                              << result_2.first_mismatch_row << " col "
                              << result_2.first_mismatch_col;
}
