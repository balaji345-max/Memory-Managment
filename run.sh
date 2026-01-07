#!/bin/bash

# Define paths
EXECUTABLE="./memsim"
TEST_DIR="./tests"
OUTPUT_DIR="./output"

# Create output directory if it doesn't exist
mkdir -p "$OUTPUT_DIR"

# 1. Compile the Project
echo "========================================"
echo " [Compiling] Memory Simulator..."
echo "========================================"
make
if [ $? -ne 0 ]; then
    echo " [ERROR] Compilation Failed!"
    exit 1
fi
echo " [SUCCESS] Compilation Successful."

# 2. Run tests and save outputs
echo ""
echo "========================================"
echo " [RUNNING TESTS]"
echo "========================================"

idx=1
found_test=false

for test_file in "$TEST_DIR"/test*; do
    if [ -f "$test_file" ]; then
        found_test=true
        output_file="$OUTPUT_DIR/output$idx"

        echo ""
        echo "----------------------------------------"
        echo " [TEST $idx]"
        echo " [INPUT ]: $test_file"
        echo " [OUTPUT]: $output_file"
        echo "----------------------------------------"

        $EXECUTABLE < "$test_file" > "$output_file"

        echo " [OK] Output saved to $output_file"
        ((idx++))
    fi
done

if [ "$found_test" = false ]; then
    echo " [WARNING] No test files found in $TEST_DIR"
fi

echo ""
echo "========================================"
echo " [DONE] All Tests Completed."
echo "========================================"
