#!/bin/bash
cd "$(dirname "$0")"

echo "=== Baseline (no predictor) ==="
base_c=0; base_t=0; base_p=0; base_n=0
for test in tests/rv32ui-p-*.hex; do
  result=$(./tinyrv -s "$test" 2>&1)
  base_n=$((base_n+1))

  if echo "$result" | grep -q "PASSED"; then
    base_p=$((base_p+1))
  fi

  line=$(echo "$result" | grep PERF)
  c=$(echo "$line" | sed 's/.*bpred=\([0-9]*\).*/\1/')
  t=$(echo "$line" | sed 's/.*bpred=[0-9]*\/\([0-9]*\).*/\1/')

  base_c=$((base_c+c))
  base_t=$((base_t+t))
done

if [ "$base_t" -gt 0 ]; then
  base_pct=$(echo "scale=3; 100 * $base_c / $base_t" | bc)
else
  base_pct="0.000"
fi

echo "  Tests: $base_p/$base_n passed, bpred: $base_c/$base_t (${base_pct}%)"

echo ""
echo "=== GShare (-g) ==="
gs_c=0; gs_t=0; gs_p=0; gs_n=0
for test in tests/rv32ui-p-*.hex; do
  result=$(./tinyrv -sg "$test" 2>&1)
  gs_n=$((gs_n+1))

  if echo "$result" | grep -q "PASSED"; then
    gs_p=$((gs_p+1))
  fi

  line=$(echo "$result" | grep PERF)
  c=$(echo "$line" | sed 's/.*bpred=\([0-9]*\).*/\1/')
  t=$(echo "$line" | sed 's/.*bpred=[0-9]*\/\([0-9]*\).*/\1/')

  gs_c=$((gs_c+c))
  gs_t=$((gs_t+t))
done

if [ "$gs_t" -gt 0 ]; then
  gs_pct=$(echo "scale=3; 100 * $gs_c / $gs_t" | bc)
else
  gs_pct="0.000"
fi

echo "  Tests: $gs_p/$gs_n passed, bpred: $gs_c/$gs_t (${gs_pct}%)"

echo ""
echo "=== GSharePlus (-gg) ==="
gp_c=0; gp_t=0; gp_p=0; gp_n=0
for test in tests/rv32ui-p-*.hex; do
  result=$(./tinyrv -sgg "$test" 2>&1)
  gp_n=$((gp_n+1))

  if echo "$result" | grep -q "PASSED"; then
    gp_p=$((gp_p+1))
  fi

  line=$(echo "$result" | grep PERF)
  c=$(echo "$line" | sed 's/.*bpred=\([0-9]*\).*/\1/')
  t=$(echo "$line" | sed 's/.*bpred=[0-9]*\/\([0-9]*\).*/\1/')

  gp_c=$((gp_c+c))
  gp_t=$((gp_t+t))
done

if [ "$gp_t" -gt 0 ]; then
  gp_pct=$(echo "scale=3; 100 * $gp_c / $gp_t" | bc)
else
  gp_pct="0.000"
fi

echo "  Tests: $gp_p/$gp_n passed, bpred: $gp_c/$gp_t (${gp_pct}%)"