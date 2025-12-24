#!/bin/bash 

set -e
set -x

cd /app/src/microbenchmark

output=validation.md

echo "## Results for security validation (Section 6.1)" > $output
echo >> $output
echo >> $output

echo "### Execution time and Cache flush time" >> $output
echo >> $output
echo '```bash' >> $output

bash vector-t-test.sh | grep -e "Processing groups" -e "max t:" >> $output

echo '```' >> $output
echo >> $output
echo >> $output

echo "### Word-level access time" >> $output
echo >> $output
echo '```bash' >> $output

bash vector-t-test-false-depen.sh | grep -e "Processing groups" -e "max t:" >> $output 
echo '```' >> $output
echo >> $output
echo >> $output
