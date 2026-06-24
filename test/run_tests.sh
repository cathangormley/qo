#!/bin/bash
# Simple test suite for qo language

PASS=0
FAIL=0
QO_BIN=${QO_BIN:-./qo}
OUT80=$(printf '"'; printf 'a%.0s' {1..78}; printf '"')
OUT81_TRUNC=$(printf '"'; printf 'c%.0s' {1..77}; printf '..')
OUT25_LINES=$(printf '1\n2\n3\n4\n5\n6\n7\n8\n9\n10\n11\n12\n13\n14\n15\n16\n17\n18\n19\n20\n21\n22\n23\n24\n..')
OUT25_LINES_EXACT=$(printf '1\n2\n3\n4\n5\n6\n7\n8\n9\n10\n11\n12\n13\n14\n15\n16\n17\n18\n19\n20\n21\n22\n23\n24\n25')

test_case() {
    local name="$1"
    local input="$2"
    local expected="$3"
    
    result=$(echo -e "$input\nexit 0" | "$QO_BIN" 2>&1 | sed 's/^qo>//' | grep -v "^Welcome" | grep -v "^Type" | grep -v "^$" | tail -1)
    
    if [ "$result" = "$expected" ]; then
        echo "[PASS] $name"
        PASS=$((PASS + 1))
    else
        echo "[FAIL] $name"
        echo "  Input:    $input"
        echo "  Expected: $expected"
        echo "  Got:      $result"
        FAIL=$((FAIL + 1))
    fi
}

test_case_file_args() {
    local name="$1"
    local script="$2"
    local expected="$3"
    shift 3

    local tmpfile=$(mktemp /tmp/qo_test_XXXXXX.qo)
    printf '%s\n' "$script" > "$tmpfile"
    result=$(echo "exit 0" | "$QO_BIN" "$tmpfile" "$@" 2>&1 | sed 's/^qo>//' | grep -v "^Welcome" | grep -v "^Type" | grep -v "^$" | tail -1)
    rm -f "$tmpfile"

    if [ "$result" = "$expected" ]; then
        echo "[PASS] $name"
        PASS=$((PASS + 1))
    else
        echo "[FAIL] $name"
        echo "  Script:   $script"
        echo "  Args:     $*"
        echo "  Expected: $expected"
        echo "  Got:      $result"
        FAIL=$((FAIL + 1))
    fi
}

test_case_multiline() {
    local name="$1"
    local input="$2"
    local expected="$3"
    local expected_lines

    expected_lines=$(printf '%s\n' "$expected" | wc -l)
    result=$(echo -e "$input\nexit 0" | "$QO_BIN" 2>&1 | sed 's/^qo>//' | grep -v "^Welcome" | grep -v "^Type" | grep -v "^$" | tail -n "$expected_lines")

    if [ "$result" = "$expected" ]; then
        echo "[PASS] $name"
        PASS=$((PASS + 1))
    else
        echo "[FAIL] $name"
        echo "  Input:    $input"
        echo "  Expected:"
        echo "$expected" | sed 's/^/    /'
        echo "  Got:"
        echo "$result" | sed 's/^/    /'
        FAIL=$((FAIL + 1))
    fi
}

cd "$(dirname "$0")/.."
echo "Running test suite..."
echo ""

# Arithmetic
test_case "addition" "1 + 2" "3"
test_case "addition_default_long_unsuffixed" "5 + 7" "12"
test_case "addition_default_long_matches_j" "1 + 2j" "3"
test_case "addition_int_preserves_int" "1i + 2i" "3i"
test_case "addition_short_preserves_short" "1h + 2h" "3h"
test_case "addition_mixed_promotes_int" "1h + 2i" "3i"
test_case "addition_mixed_promotes_long" "1i + 2j" "3"
test_case "negative_literal_in_expression" "1 + -2" "-1"
test_case "negative_literal_multiplication" "2 * -3" "-6"
test_case "negative_float_literal" "1 + -.5" "0.5f"
test_case "vector_with_negative_int_element" "1 -2 4" "1 -2 4"
test_case "vector_with_negative_float_element" "1 -0.5 4" "1 -0.5 4f"
test_case "first_of_int_vector" "a:(1;2;3); first a" "1"
test_case "last_of_int_vector" "a:(1;2;3); last a" "3"
test_case "first_of_list" "a:(enlist 1; enlist 2; enlist 3); first a" "1"
test_case "last_of_list" "a:(enlist 1; enlist 2; enlist 3); last a" "3"
test_case "first_of_atom" "first 42" "42"
test_case "last_of_atom" "last 42" "42"
test_case "first_empty_long"  'first "j"$()'  "0N"
test_case "last_empty_long"   'last "j"$()'   "0N"
test_case "first_empty_int"   'first "i"$()'  "0Ni"
test_case "last_empty_int"    'last "i"$()'   "0Ni"
test_case "first_empty_short" 'first "h"$()'  "0Nh"
test_case "last_empty_short"  'last "h"$()'   "0Nh"
test_case "first_empty_float" 'first "f"$()'  "0Nf"
test_case "last_empty_float"  'last "f"$()'   "0Nf"
test_case "first_empty_bool"  'first "b"$()'  "0b"
test_case "last_empty_bool"   'last "b"$()'   "0b"
test_case "first_empty_list"  'first ()'      ""
test_case "last_empty_list"   'last ()'       ""
test_case "shell_echo" "shell \"echo hello\"" "\"hello\""
test_case "shell_assign" "x: shell \"echo test\"; x" "\"test\""
test_case "drop_positive" "3 _ (5;6;7;8;9)" "8 9"
test_case "drop_negative" "-3 _ (1;2;3;4;5)" "1 2"
test_case "drop_zero" "0 _ (1;2;3)" "1 2 3"
test_case "drop_all" "5 _ (1;2;3;4;5)" "[]"
test_case "drop_from_atom" "0 _ 42" "42"
test_case "subtraction" "10 - 3" "7"
test_case "subtraction_int_preserves_int" "5i - 2i" "3i"
test_case "subtraction_mixed_promotes_long" "5i - 2j" "3"
test_case "large_int_multiply_exact" "5000000001*1000000001" "5000000006000000001"
test_case "percent_is_division" "5 % 2" "2.5f"
test_case "division_by_zero_pos" "1 % 0" "0Wf"
test_case "division_by_zero_neg" "-1 % 0" "-0Wf"
test_case "division_by_zero_zero" "0 % 0" "0Nf"
test_case "division_by_zero_float" "1f % 0f" "0Wf"
test_case "division_by_zero_vector" "1 2 3 % 0" "0W 0W 0Wf"
test_case "power_basic" "2 ** 3" "8f"
test_case "power_float_base" "2.5 ** 2" "6.25f"
test_case "power_float_exponent" "4 ** 0.5" "2f"
test_case "power_negative_exponent" "2 ** -1" "0.5f"
test_case "power_zero_exponent" "5 ** 0" "1f"
test_case "power_one_exponent" "5 ** 1" "5f"
test_case "power_right_to_left" "2 ** 3 + 1" "16f"
test_case "power_callable_form" "**[2;3]" "8f"
test_case "power_vector_base" "2 3 4 ** 2" "4 9 16f"
test_case "power_vector_exponent" "2 ** 1 2 3" "2 4 8f"
test_case "power_vector_vector" "2 3 4 ** 1 2 3" "2 9 64f"
test_case_multiline "power_dict" "d:(1;2;3)!(4;5;6); 2 ** d" $'1 | 16f\n2 | 32f\n3 | 64f'
test_case "slash_undefined" "5 / 2" "Error: operator '/' is undefined"
test_case "right_to_left_addition_comma" "10 + 10, 20" "20 30"
test_case "right_to_left_comma_addition" "10, 20 + 5" "10 25"
test_case "equal_atom_true" "3=3" "1b"
test_case "equal_atom_false" "3=4" "0b"
test_case "equal_atom_vector" "3=(1;3;5)" "010b"
test_case "equal_vector_atom" "(1;3;5)=3" "010b"
test_case "equal_vector_vector" "(1;3;5)=(1;2;5)" "101b"
test_case "equal_vector_length_mismatch" "(1;2)=(1;2;3)" "Error: cannot compare vectors of different lengths"
test_case "less_atom_true" "1<2" "1b"
test_case "less_atom_false" "2<1" "0b"
test_case "greater_atom_true" "3>2" "1b"
test_case "greater_atom_false" "2>3" "0b"
test_case "less_atom_vector" "3<(1;3;5)" "001b"
test_case "greater_atom_vector" "3>(1;3;5)" "100b"
test_case "less_vector_atom" "(1;3;5)<3" "100b"
test_case "greater_vector_atom" "(1;3;5)>3" "001b"
test_case "less_vector_vector" "(1;4;3)<(2;2;3)" "100b"
test_case "greater_vector_vector" "(1;4;3)>(2;2;3)" "010b"
test_case "less_vector_length_mismatch" "(1;2)<(1;2;3)" "Error: cannot compare vectors of different lengths"
test_case "greater_vector_length_mismatch" "(1;2)>(1;2;3)" "Error: cannot compare vectors of different lengths"
test_case "lte_atom_true" "1<=2" "1b"
test_case "lte_atom_equal" "2<=2" "1b"
test_case "lte_atom_false" "3<=2" "0b"
test_case "gte_atom_true" "3>=2" "1b"
test_case "gte_atom_equal" "2>=2" "1b"
test_case "gte_atom_false" "1>=2" "0b"
test_case "lte_atom_vector" "3<=(1;3;5)" "011b"
test_case "gte_atom_vector" "3>=(1;3;5)" "110b"
test_case "lte_vector_atom" "(1;3;5)<=3" "110b"
test_case "gte_vector_atom" "(1;3;5)>=3" "011b"
test_case "lte_vector_vector" "(1;4;3)<=(2;2;3)" "101b"
test_case "gte_vector_vector" "(1;4;3)>=(2;2;3)" "011b"
test_case "lte_vector_length_mismatch" "(1;2)<=(1;2;3)" "Error: cannot compare vectors of different lengths"
test_case "gte_vector_length_mismatch" "(1;2)>=(1;2;3)" "Error: cannot compare vectors of different lengths"

# Vectors
test_case "vector_creation" "1, 2, 3" "1 2 3"
test_case "vector_join" "(1, 2), (3, 4)" "1 2 3 4"
test_case "vector_index_bracket" "(3,2)[0]" "3"
test_case "vector_index_implicit_apply" "(3,2) 0" "3"
test_case "vector_multi_index" "a:3 4 5; a[1 2]" "4 5"
test_case "vector_multi_index_reverse" "a:3 4 5; a[2 1 0]" "5 4 3"
test_case "vector_multi_index_dup" "a:3 4 5; a[0 0]" "3 3"
test_case "vector_multi_index_list" "a:3 4 5; a[(0;2)]" "3 5"
test_case "list_multi_index" "a:(10;20;30); a[0 2]" "10 30"
test_case "char_vec_multi_index" "a:\"hello\"; a[0 1 2]" "\"hel\""
test_case "multi_index_oob" "a:3 4 5; a[0 5]" "3 0N"
test_case "single_index_oob" "(3 4 5) 5" "0N"
test_case "single_index_oob_int" "(3 4 5i) 5" "0Ni"
test_case "single_index_oob_short" "(3 4 5h) 5" "0Nh"
test_case "single_index_oob_float" "(3 4 5f) 5" "0Nf"
test_case "bracket_negative_index" "(3 4 5)[-1]" "0N"
test_case_multiline "dict_multi_index" "d:til[10]!2*til 10; d[5 6 7]" $'10\n12\n14'
test_case_multiline "dict_multi_index_list" "d:til[10]!2*til 10; d[(4;6)]" $'8\n12'
test_case "dict_multi_index_missing" "d:(1;2;3)!(10;20;30); d[1 9]" "Error: dictionary key not found"
test_case "empty_list_literal" "()" ""
test_case "list_literal" "(1;2;3)" "1 2 3"
test_case "list_index_bracket" "(3;2)[0]" "3"
test_case_multiline "join_longvec_symvec" "1 2, \`a\`b" $'1\n2\n\x60a\n\x60b'
test_case_multiline "join_long_scalar_symvec" "1, \`a\`b" $'1\n\x60a\n\x60b'
test_case_multiline "join_long_sym_scalars" "1, \`a" $'1\n\x60a'
test_case "join_list_longvec_type" "type ((1;2), 3 4)" "\`LONG"
test_case "join_longvec_symvec_type" "type (1 2, \`a\`b)" "\`list"
test_case_multiline "join_three_ways" "1 2, \`a\`b, 3 4f" $'1\n2\n\x60a\n\x60b\n3f\n4f'
test_case_multiline "join_scalar_mixed_list" "1, (3;\`a)" $'1\n3\n\x60a'
test_case_multiline "join_intvec_floatvec" "1 2, 3.5 4.5f" $'1\n2\n3.5f\n4.5f'
test_case "join_intvec_floatvec_type" "type (1 2, 3.5 4.5f)" "\`list"

# Element-wise operations
test_case "vector_plus_atom" "(10, 20) + 5" "15 25"
test_case "atom_times_vector" "2 * (1, 2, 3)" "2 4 6"
test_case "list_plus_atom" "(1;2;3) + 5" "6 7 8"
test_case_multiline "list_of_vectors_prints_newlines" "(3#1;2#2)" $'1 1 1\n2 2'
test_case_multiline "top_level_row_list_nested_list_old_style" "(5#1;(2#3;4#5))" $'1 1 1 1 1\n(3 3;5 5 5 5)'
test_case_multiline "output_exactly_25_lines" "(1#1;1#2;1#3;1#4;1#5;1#6;1#7;1#8;1#9;1#10;1#11;1#12;1#13;1#14;1#15;1#16;1#17;1#18;1#19;1#20;1#21;1#22;1#23;1#24;1#25)" "$OUT25_LINES_EXACT"
test_case_multiline "output_truncates_after_25_lines" "(1#1;1#2;1#3;1#4;1#5;1#6;1#7;1#8;1#9;1#10;1#11;1#12;1#13;1#14;1#15;1#16;1#17;1#18;1#19;1#20;1#21;1#22;1#23;1#24;1#25;1#26)" "$OUT25_LINES"

# List arithmetic (nested lists of differing sizes)
test_case_multiline "nested_list_plus_atom" "(1 2;3;4 5 6) + 1" $'2 3\n4\n5 6 7'
test_case_multiline "nested_list_plus_vector" "(1 2;3;4 5 6) + 1 2 3" $'2 3\n5\n7 8 9'
test_case_multiline "nested_list_plus_list" "(1 2;3;4 5 6) + (1 2;3;4)" $'2 4\n6\n8 9 10'
test_case "nested_list_plus_list_inner_mismatch" "(1 2;3;4 5 6) + (1 2;3;4 5)" "Error: cannot operate on vectors of different lengths"
test_case "nested_list_len_mismatch" "(1 2;3) + (1;2;3)" "Error: list arithmetic: length mismatch"

test_case_multiline "nested_list_minus_atom" "(1 2;3;4 5 6) - 1" $'0 1\n2\n3 4 5'
test_case_multiline "nested_list_minus_vector" "(1 2;3;4 5 6) - 1 2 3" $'0 1\n1\n1 2 3'
test_case_multiline "nested_list_minus_list" "(1 2;3;4 5 6) - (1 2;3;4)" $'0 0\n0\n0 1 2'
test_case "nested_list_minus_list_inner_mismatch" "(1 2;3;4 5 6) - (1 2;3;4 5)" "Error: cannot operate on vectors of different lengths"
test_case "nested_list_minus_len_mismatch" "(1 2;3) - (1;2;3)" "Error: list arithmetic: length mismatch"

test_case_multiline "nested_list_mul_atom" "(1 2;3;4 5 6) * 2" $'2 4\n6\n8 10 12'
test_case_multiline "nested_list_mul_vector" "(1 2;3;4 5 6) * 1 2 3" $'1 2\n6\n12 15 18'
test_case_multiline "nested_list_mul_list" "(1 2;3;4 5 6) * (1 2;3;4)" $'1 4\n9\n16 20 24'
test_case "nested_list_mul_list_inner_mismatch" "(1 2;3;4 5 6) * (1 2;3;4 5)" "Error: cannot operate on vectors of different lengths"
test_case "nested_list_mul_len_mismatch" "(1 2;3) * (1;2;3)" "Error: list arithmetic: length mismatch"

test_case_multiline "nested_list_div_atom" "(4 6;9;12 15 18) % 2" $'2 3f\n4.5f\n6 7.5 9f'
test_case_multiline "nested_list_div_vector" "(4 6;9;12 15 18) % 2 3 4" $'2 3f\n3f\n3 3.75 4.5f'
test_case_multiline "nested_list_div_list" "(4 6;9;12 15 18) % (2 3;3;4 5 6)" $'2 2f\n3f\n3 3 3f'
test_case "nested_list_div_list_inner_mismatch" "(4 6;9;12 15 18) % (2 3;3;4 5)" "Error: cannot operate on vectors of different lengths"
test_case "nested_list_div_len_mismatch" "(4 6;9) % (2;3;4)" "Error: list arithmetic: length mismatch"

test_case_multiline "nested_list_pow_atom" "(2 3;4;5 6 7) ** 2" $'4 9f\n16f\n25 36 49f'
test_case_multiline "nested_list_pow_vector" "(2 3;4;5 6 7) ** 1 2 3" $'2 3f\n16f\n125 216 343f'
test_case_multiline "nested_list_pow_list" "(2 3;4;5 6 7) ** (2 3;3;4 5 6)" $'4 27f\n64f\n625 7776 117649f'
test_case "nested_list_pow_list_inner_mismatch" "(2 3;4;5 6 7) ** (2 3;3;4 5)" "Error: cannot operate on vectors of different lengths"
test_case "nested_list_pow_len_mismatch" "(2 3;4) ** (2;3;4)" "Error: list arithmetic: length mismatch"

test_case "take_list_wrap" "5#(1;2;3)" "1 2 3 1 2"
test_case "take_negative_from_back" "-2#(1;2;3;4)" "3 4"
test_case "take_negative_wrap_from_back" "-5#(1;2;3)" "2 3 1 2 3"
test_case "take_negative_char_from_back" "-2#\"abcd\"" "\"cd\""
test_case "take_negative_atom_same_result" "-3#1" "1 1 1"
test_case "take_atom_promotes_vector" "5#1" "1 1 1 1 1"

# Variables
test_case "variable_assignment" "x : 42\nx" "42"
test_case "undefined_variable_errors" "a" "Error: undefined variable 'a'"
test_case "vector_assignment" "v : 1, 2, 3\nv" "1 2 3"
test_case "vector_in_expr" "w : 5, 10\nw + 1" "6 11"
test_case "refcount_chain_expression" "refcount[a:b:c:100]" "3"
test_case "list_assignment_and_use" "a:(1;2;3)+5; a" "6 7 8"
test_case "infix_assignment_in_expression" "2 + a:3" "5"
test_case "eval_assignment_tree" "eval(:;\`a;3)" "3"
test_case "symbol_literal_distinct_from_var" "a:5; \`a" "\`a"
test_case "empty_symbol_literal" "\`" "\`"
test_case "empty_symbol_in_list" "(\`;\`a;\`b)" "\`\`a\`b"
test_case "symbol_literal_equality" "\`foo=\`foo" "1b"
test_case "symbol_literal_inequality" "\`foo=\`bar" "0b"
test_case "symbol_assignment_equality" "a:\`foo; b:\`foo; a=b" "1b"
test_case "symbol_assignment_stable_after_new_symbol" "a:\`foo; b:\`bar; c:\`foo; a=c" "1b"
test_case "failed_assignment_does_not_bind" "a:2+\`sym\na" "Error: undefined variable 'a'"
test_case "error_halts_nested_evaluation" "shell\"sleep 2\",2+\`a" "Error: arithmetic operations require numeric operands"
test_case "exit_stops_sequence_immediately" "exit 1; print \"hello\"; exit 2" ""
test_case "exit_bracket_stops_sequence" "exit[0]; print \"hello\"" ""

# Strings and Characters
test_case "char_literal" "first \"a\"" "'a'"
test_case "string_literal" "\"hello\"" "\"hello\""
test_case "print_exact_80_chars" "78#first \"a\"" "$OUT80"
test_case "print_truncates_over_80_chars" "79#first \"c\"" "$OUT81_TRUNC"
test_case "char_concatenation" "(first \"h\"),(first \"i\")" "\"hi\""
test_case "char_string_concat" "(first \"a\"), \"bc\"" "\"abc\""
test_case "string_concatenation" "\"hello\", \" \", \"world\"" "\"hello world\""
test_case "print_keyword_string" "print \"hello\"" "hello"
test_case "print_bracket_call_string" "print[\"world\"]" "world"
test_case "string_assignment" "s : \"test\"\ns" "\"test\""
test_case "char_assignment" "c : first \"x\"\nc" "'x'"

# Semicolon-separated statements
test_case "two_statements" "x : 5; x + 1" "6"
test_case "three_statements" "a : 2 + 2; b : a + 1; a + b" "9"
test_case "statement_with_vector" "v : 1, 2, 3; v + 10" "11 12 13"

# Function calls and keywords
test_case "sum_keyword" "a : (1;2;3); sum a" "6"
test_case "count_list" "count (1;2;3)" "3"
test_case "count_vector" "count (1,2,3)" "3"
test_case "count_char_vector" "count \"abc\"" "3"
test_case "count_dict"         "count \`a\`b!1 2" "2"
test_case "count_table"        "count ([]a:1 2 3;b:4 5 6)" "3"
test_case "count_composition"   "count compose (+;-)"        "2"
test_case "count_function"      "count {[x] x*2}"           "1"
test_case "count_scalar"        "count 42"                  "1"
test_case "count_null"          "count ()"                  "0"
test_case "count_projection"    "count (+[;2])"             "1"
test_case "til_long" "til[5]" "0 1 2 3 4"
test_case "til_int" "til[5i]" "0 1 2 3 4"
test_case "til_short" "til[5h]" "0 1 2 3 4"
test_case "min_long_vector" "min[5 2 9]" "2"
test_case "max_long_vector" "max[5 2 9]" "9"
test_case "min_int_vector" "min[5 2 9i]" "2i"
test_case "max_int_vector" "max[5 2 9i]" "9i"
test_case "min_short_vector" "min[5 2 9h]" "2h"
test_case "max_short_vector" "max[5 2 9h]" "9h"
test_case "min_float_vector" "min[5 2.5 9]" "2.5f"
test_case "max_float_vector" "max[5 2.5 9]" "9f"
test_case "min_scalar_short" "min[4h]" "4h"
test_case "max_scalar_int" "max[4i]" "4i"
test_case "min_scalar_long" "min[4]" "4"
test_case "sum_bracket_call" "a : (1;2;3); sum[a]" "6"
test_case "plus_bracket_call" "+[2;3]" "5"
test_case "enlist_basic" "enlist[1;2;3]" "1 2 3"
test_case "enlist_var_resolves" "a:5; enlist[a;10]" "5 10"
test_case "enlist_symbol_literal_stays_symbol" "type enlist[\`a;10]" "\`list"
test_case "enlist_homogeneous_long_promotes_vector" "enlist[1;2;3]" "1 2 3"
test_case "enlist_homogeneous_int_promotes_vector" "enlist[1i;2i;3i]" "1 2 3i"
test_case "enlist_homogeneous_short_promotes_vector" "enlist[1h;2h;3h]" "1 2 3h"
test_case "enlist_homogeneous_float_promotes_vector" "enlist[1f;2f;3f]" "1 2 3f"
test_case "enlist_homogeneous_char_promotes_string" "enlist[first \"a\";first \"b\";first \"c\"]" "\"abc\""
test_case "enlist_homogeneous_bool_promotes_bool_vector" "enlist[1b;0b;1b]" "101b"
test_case "enlist_homogeneous_byte_promotes_byte_vector" "enlist[0x01;0x02;0x03]" "0x010203"
test_case "enlist_homogeneous_symbol_promotes_sym_vector" "enlist[\`a;\`b;\`c]" "\`a\`b\`c"
test_case "enlist_mixed_types_stays_list" "type enlist[1;2f;3]" "\`list"
test_case "enlist_projection_type" "type enlist[3;]" "\`projection"
test_case "enlist_projection_apply" "enlist[3;][5]" "3 5"
test_case "enlist_projection_first_slot" "enlist[;5][3]" "3 5"
test_case "enlist_projection_three_args" "enlist[1;;3][2]" "1 2 3"
test_case "string_long_scalar" "string 123" "\"123\""
test_case "string_int_scalar" "string 5i" "\"5\""
test_case "string_short_scalar" "string 7h" "\"7\""
test_case "string_float_scalar" "string 1.5" "\"1.5\""
test_case "string_bool_scalar" "string 1b" "\"1\""
test_case "string_char_scalar" "string \"a\"" "\"a\""
test_case "string_symbol" "string \`foo" "\"foo\""
test_case_multiline "string_charvec_atomic" "string \"hello\"" $'"h"\n"e"\n"l"\n"l"\n"o"'
test_case_multiline "string_charvec_two_chars" "string \"ab\"" $'"a"\n"b"'
test_case "string_charvec_type" "type string \"hello\"" "\`list"
test_case "string_empty_charvec_type" "type string \"\"" "\`list"
test_case "string_empty_charvec_count" "count string \"\"" "0"
test_case "string_charvec_count" "count string \"hello\"" "5"
test_case "string_null_long" "string 0N" "\"0N\""
test_case "string_inf_long" "string 0W" "\"0W\""
test_case_multiline "string_long_vector" "string 11 22 33" $'"11"\n"22"\n"33"'
test_case_multiline "string_int_vector" "string 1 2 3i" $'"1"\n"2"\n"3"'
test_case_multiline "string_float_vector" "string 1.5 2.5 3.5" $'"1.5"\n"2.5"\n"3.5"'
test_case_multiline "string_sym_vector" "string \`a\`b\`c" $'"a"\n"b"\n"c"'
test_case_multiline "string_mixed_list" "string (1;2.5;\`a)" $'"1"\n"2.5"\n"a"'
test_case "string_scalar_type" "type string 123" "\`CHAR"
test_case "string_vector_type" "type string 11 22 33" "\`list"
test_case "sum_inline_call" "sum[(1;2;3)]" "6"
test_case "sum_scalar" "sum 5" "5"
test_case "keyword_alias_call" "a:sum; a (5;4;3)" "12"
test_case "now_type" "type now[]" "\`timestamp"
test_case "now_positive" "now[] > 2020.01.01T00:00:00.000000000" "1b"
test_case "timestamp_scalar_parse_print" "2026.05.31T00:11:00.123456789" "2026.05.31T00:11:00.123456789"
test_case "timestamp_scalar_type" "type 2026.05.31T00:11:00.123456789" "\`timestamp"
test_case "timestamp_var_roundtrip" "t:2026.05.31T00:11:00.123456789; t" "2026.05.31T00:11:00.123456789"
test_case "timestamp_epoch" "1970.01.01T00:00:00.000000000" "1970.01.01T00:00:00.000000000"
test_case "timestamp_leap_day" "2024.02.29T23:59:59.999999999" "2024.02.29T23:59:59.999999999"
test_case "timestamp_vector_print" \
    "2026.05.31T00:11:00.000000000 2026.06.01T12:00:00.000000000" \
    "2026.05.31T00:11:00.000000000 2026.06.01T12:00:00.000000000"
test_case "timestamp_vector_type" \
    "type 2026.05.31T00:11:00.000000000 2026.06.01T12:00:00.000000000" \
    "\`TIMESTAMP"
test_case "timestamp_vector_count" \
    "count 2026.05.31T00:11:00.000000000 2026.06.01T12:00:00.000000000 2026.06.02T12:00:00.000000000" \
    "3"
test_case "timestamp_equal" \
    "2026.05.31T00:11:00.123456789 = 2026.05.31T00:11:00.123456789" \
    "1b"
test_case "timestamp_less" \
    "2026.05.31T00:11:00.000000000 < 2026.06.01T12:00:00.000000000" \
    "1b"
test_case "timestamp_first" \
    "first 2026.05.31T00:11:00.000000000 2026.06.01T12:00:00.000000000" \
    "2026.05.31T00:11:00.000000000"
test_case "timestamp_first_type" \
    "type first 2026.05.31T00:11:00.000000000 2026.06.01T12:00:00.000000000" \
    "\`timestamp"
test_case "timestamp_string" \
    "string 2026.05.31T00:11:00.123456789" \
    "\"2026.05.31T00:11:00.123456789\""
test_case "timestamp_date_only"          "2026.01.01T"             "2026.01.01T00:00:00.000000000"
test_case "timestamp_with_hour"          "2026.01.01T01"           "2026.01.01T01:00:00.000000000"
test_case "timestamp_with_minute"        "2026.01.01T01:02"        "2026.01.01T01:02:00.000000000"
test_case "timestamp_with_second"        "2026.01.01T01:02:03"     "2026.01.01T01:02:03.000000000"
test_case "timestamp_with_one_frac"      "2026.01.01T01:02:03.4"   "2026.01.01T01:02:03.400000000"
test_case "timestamp_with_three_frac"    "2026.01.01T01:02:03.123" "2026.01.01T01:02:03.123000000"
test_case "timestamp_with_full_frac"     "2026.01.01T01:02:03.123456789" "2026.01.01T01:02:03.123456789"
test_case "timestamp_partial_equals_full" \
    "2026.01.01T01:02:03.4 = 2026.01.01T01:02:03.400000000" "1b"
test_case "timestamp_date_only_equals_midnight" \
    "2026.01.01T = 2026.01.01T00:00:00.000000000" "1b"
test_case "timestamp_date_only_type" "type 2026.01.01T" "\`timestamp"
test_case "timestamp_plus_long_type"        "type 2026.01.01T + 1"          "\`timestamp"
test_case "timestamp_plus_long_value"       "2026.01.01T + 1000000000"      "2026.01.01T00:00:01.000000000"
test_case "timestamp_minus_long_value"      "2026.01.01T - 1000000000"      "2025.12.31T23:59:59.000000000"
test_case "long_plus_timestamp_type"        "type 1 + 2026.01.01T"          "\`timestamp"
test_case "timestamp_plus_int_type"         "type 2026.01.01T + 1i"         "\`timestamp"
test_case "timestamp_plus_short_type"       "type 2026.01.01T + 1h"         "\`timestamp"
test_case "timestamp_times_long_type"       "type 2026.01.01T * 2"          "\`timestamp"
test_case "timestamp_plus_long_vector_type" "type 2026.01.01T + 1 2 3"      "\`TIMESTAMP"
test_case "timestamp_vector_plus_long_type" \
    "type 2026.01.01T 2026.01.02T + 1" "\`TIMESTAMP"
test_case "timestamp_vector_plus_long_vector_type" \
    "type 2026.01.01T 2026.01.02T + 1 2" "\`TIMESTAMP"
test_case "timestamp_vector_plus_long_vector_value" \
    "2026.01.01T 2026.01.02T + 1 2" \
    "2026.01.01T00:00:00.000000001 2026.01.02T00:00:00.000000002"
test_case "timestamp_vector_minus_short_type" \
    "type 2026.01.01T 2026.01.02T - 1h" "\`TIMESTAMP"
test_case "timestamp_roundtrip_add_subtract" \
    "a:2026.05.31T00:11:00.123456789; b:(a + 5) - 5; a = b" "1b"

# timespan tests
test_case "timespan_full_format"       "1T00:01:00.000000000"              "1T00:01:00.000000000"
test_case "timespan_type"              "type 1T00:01:00.000000000"         "\`timespan"
test_case "timespan_var_roundtrip"     "t:1T00:01:00.000000000; t"         "1T00:01:00.000000000"
test_case "timespan_zero"              "0T"                                "0T00:00:00.000000000"
test_case "timespan_multi_day"         "365T00:00:00.000000000"            "365T00:00:00.000000000"
test_case "timespan_vector_print" \
    "1T 2T" "1T00:00:00.000000000 2T00:00:00.000000000"
test_case "timespan_vector_type" \
    "type 1T 2T" "\`TIMESPAN"
test_case "timespan_vector_count" \
    "count 1T 2T 3T" "3"
test_case "timespan_equal"             "1T00:01:00.000000000 = 1T00:01:00.000000000" "1b"
test_case "timespan_not_equal"         "1T = 2T"                              "0b"
test_case "timespan_less"              "1T < 2T"                              "1b"
test_case "timespan_greater"           "2T > 1T"                              "1b"
test_case "timespan_first" \
    "first 1T 2T" "1T00:00:00.000000000"
test_case "timespan_first_type" \
    "type first 1T 2T" "\`timespan"
test_case "timespan_string" \
    "string 1T00:01:00.000000000"  "\"1T00:01:00.000000000\""
test_case "timespan_date_only"         "1T"                   "1T00:00:00.000000000"
test_case "timespan_with_hour"         "1T01"                 "1T01:00:00.000000000"
test_case "timespan_with_minute"       "1T01:02"              "1T01:02:00.000000000"
test_case "timespan_with_second"       "1T01:02:03"           "1T01:02:03.000000000"
test_case "timespan_with_one_frac"     "1T01:02:03.4"         "1T01:02:03.400000000"
test_case "timespan_with_full_frac"    "1T01:02:03.123456789" "1T01:02:03.123456789"
test_case "timespan_partial_equals_full" \
    "1T01:02 = 1T01:02:00.000000000" "1b"
test_case "timespan_plus_long_type"    "type 1T + 1"           "\`timespan"
test_case "timespan_plus_long_value"   "1T + 1000000000"       "1T00:00:01.000000000"
test_case "timespan_minus_long_value"  "1T - 1000000000"       "0T23:59:59.000000000"
test_case "long_plus_timespan_type"    "type 1 + 1T"           "\`timespan"
test_case "timespan_plus_int_type"     "type 1T + 1i"          "\`timespan"
test_case "timespan_plus_short_type"   "type 1T + 1h"          "\`timespan"
test_case "timespan_times_long_type"   "type 1T00:01:00 * 2"   "\`timespan"
test_case "timespan_times_long_value"  "2T * 3"                "6T00:00:00.000000000"
test_case "timespan_plus_long_vector_type" \
    "type 1T + 1 2 3" "\`TIMESPAN"
test_case "timespan_vector_plus_long_type" \
    "type 1T 2T + 1" "\`TIMESPAN"
test_case "timespan_vector_plus_long_vector_type" \
    "type 1T 2T + 1 2" "\`TIMESPAN"
test_case "timespan_vector_plus_long_vector_value" \
    "1T 2T + 1 2" "1T00:00:00.000000001 2T00:00:00.000000002"
test_case "timespan_vector_minus_short_type" \
    "type 1T 2T - 1h" "\`TIMESPAN"
test_case "timespan_roundtrip_add_subtract" \
    "a:1T00:01:00.000000001; b:(a + 5) - 5; a = b" "1b"
test_case "timespan_null" "null 0T" "0b"
test_case "timespan_ser_deser" "deser ser 1T00:01:00" "1T00:01:00.000000000"
test_case "timespan_cast_to_timespan" "\$[\`timespan; 5]" "0T00:00:00.000000005"
test_case "timespan_cast_from_long" "\$[\`long; 1T]" "86400000000000"
test_case "timespan_first_of_vector" "first 1T 2T 3T" "1T00:00:00.000000000"

# ── Date type ──────────────────────────────────────────────────────────
test_case "date_scalar_parse_print"    "2026.01.15"            "2026.01.15"
test_case "date_scalar_type"           "type 2026.01.15"       "\`date"
test_case "date_epoch"                 "1970.01.01"            "1970.01.01"
test_case "date_var_roundtrip"         "d:2026.06.21; d"       "2026.06.21"
test_case "date_after_epoch"           "2026.01.01 > 1970.01.01"  "1b"
test_case "date_less"                  "2026.01.01 < 2026.12.31"  "1b"
test_case "date_equal"                 "2026.01.01 = 2026.01.01"  "1b"
test_case "date_not_equal"             "2026.01.01 = 2026.01.02"  "0b"
test_case "date_vector"                "2026.01.01 2026.06.21"    "2026.01.01 2026.06.21"
test_case "date_vector_type"           "type 2026.01.01 2026.06.21"  "\`DATE"
test_case "date_plus_long"             "2026.01.01 + 1"         "2026.01.02"
test_case "date_plus_long_type"        "type[2026.01.01 + 1]"   "\`date"
test_case "date_minus_long"            "2026.01.10 - 3"         "2026.01.07"
test_case "date_minus_date"            "2026.01.10 - 2026.01.01"   "9"
test_case "date_minus_date_type"       "type[2026.01.10 - 2026.01.01]"  "\`long"
test_case "long_plus_date"             "5 + 2026.01.01"         "2026.01.06"
test_case "date_vec_plus_long"         "2026.01.01 2026.01.02 + 1"  "2026.01.02 2026.01.03"
test_case "date_vec_minus_date_vec"    "2026.01.10 2026.01.20 - 2026.01.01 2026.01.01"  "9 19"
test_case "date_count"                 "count 2026.01.15 2026.06.21"  "2"
test_case "date_first"                 "first 2026.01.01 2026.01.15"  "2026.01.01"

test_case "empty_brackets_passes_null" "f:{[x] type x}; f[]" "\`builtin"
test_case "read_file" "read \"test/read_fixture.txt\"" "\"Hello world!\""
test_case "read_file_with_at_apply" "read @ \"test/read_fixture.txt\"" "\"Hello world!\""
test_case "read_alias_with_at_apply" "r:read; r @ \"test/read_fixture.txt\"" "\"Hello world!\""
test_case "read_missing_file" "read \"test/does_not_exist.txt\"" "Error: cannot open file 'test/does_not_exist.txt'"
test_case "sum_keyword_normal_precedence" "a : 1 2 3; sum a + 5" "21"
test_case "sum_bracket_precedence" "a : (1;2;3); sum[a] + 5" "11"
test_case "not_vector" "not[(1;2;0)]" "001b"
test_case "not_scalar_true" "not[0]" "1b"
test_case "not_scalar_false" "not[5]" "0b"
test_case "not_float_zero" "not[0f]" "1b"
test_case "not_short_zero" "not[0h]" "1b"
test_case "not_short_nonzero" "not[1h]" "0b"
test_case "not_int_zero" "not[0i]" "1b"
test_case "not_int_nonzero" "not[1i]" "0b"
test_case "not_long_explicit_zero" "not[0j]" "1b"
test_case "not_long_explicit_nonzero" "not[1j]" "0b"
test_case "not_float_nonzero" "not[1f]" "0b"
test_case "not_float_negative" "not[-2.5f]" "0b"
test_case "not_negative" "not[-5]" "0b"
test_case "not_null_long" "not[0N]" "0b"
test_case "not_short_vec" "not[1 0 1h]" "010b"
test_case "not_short_vec_all_zero" "not[0 0 0h]" "111b"
test_case "not_int_vec" "not[1 0 1i]" "010b"
test_case "not_int_vec_all_one" "not[1 1 1i]" "000b"
test_case "not_long_vec" "not[1 0 1]" "010b"
test_case "not_long_vec_all_zero" "not[0 0 0]" "111b"
test_case "not_float_vec" "not[1 0 1f]" "010b"
test_case "not_float_vec_all_zero" "not[0 0 0f]" "111b"
test_case "not_bool_scalar_false" "not[0b]" "1b"
test_case "not_bool_scalar_true" "not[1b]" "0b"
test_case "not_bool_vec" "not[101b]" "010b"
test_case "not_empty_list" "not[()]" "b"

# null builtin
test_case "null_type"                "type null"              "\`builtin"
test_case "null_scalar_null"         "null 0N"                "1b"
test_case "null_scalar_int"          "null 42"                "0b"
test_case "null_scalar_int_null"     "null 0Ni"               "1b"
test_case "null_scalar_float_null"   "null 0Nf"               "1b"
test_case "null_scalar_inf"          "null 0W"                "0b"
test_case "null_scalar_neginf"       "null -0W"               "0b"
test_case "null_vector_no_nulls"     "null 1 2 3"             "000b"
test_case "null_vector_with_null"    "null 1 0N 2"            "010b"
test_case "null_vector_multi_nulls"  "null 0N 1 0N 2"         "1010b"
test_case "null_float_vector"        "null 0N 2.5f"           "10b"
test_case "null_bool_scalar"         "null 1b"                "0b"
test_case "null_bool_vector"         "null[101b]"             "000b"
test_case "null_char_scalar"         "null \"a\""             "0b"
test_case "null_string"              "null \"abc\""           "000b"
test_case "null_byte_vec"            "null 0x0102"            "00b"
test_case "null_list_with_nulls"     "null (1;0N;3)"          "010b"
test_case "null_list_no_nulls"       "null (1;2;3)"           "000b"
test_case "null_empty_bracket"       "null[]"                 "1b"
test_case "null_sym_scalar"          "null \`a"               "0b"

# null_op (::) builtin
test_case "null_op_type"              "type ::"               "\`builtin"
test_case "null_op_identity_long"     ":: 42"                 "42"
test_case "null_op_identity_int"      ":: 42i"                "42i"
test_case "null_op_identity_short"    ":: 42h"                "42h"
test_case "null_op_identity_float"    ":: 3.5f"               "3.5f"
test_case "null_op_identity_string"   ":: \"hello\""           "\"hello\""
test_case "null_op_identity_symbol"   ":: \`a"                "\`a"
test_case "null_op_identity_char"     ":: \"x\""               "\"x\""
test_case "null_op_identity_bool"     ":: 1b"                 "1b"
test_case "null_op_bracket"           "::[99]"                "99"
test_case "null_op_identity_longvec"  ":: 1 2 3"              "1 2 3"
test_case "null_op_identity_floatvec" ":: 1.5 2.5 3.5f"       "1.5 2.5 3.5f"
test_case "null_op_identity_list"     ":: (1;2;3)"     "1 2 3"
test_case "null_op_null_self"         "null ::"               "1b"
test_case "null_op_null_apply"        "null ::[]"             "1b"
test_case "null_op_null_scalar"       "null ::[0N]"           "1b"
test_case "null_op_null_nonnull"      "null ::[5]"            "0b"
test_case "null_op_count"             "count ::"              "1"
test_case "null_op_each"              "::' [1 2 3]"           "1 2 3"

test_case "rtl_fn_over_op"           "a:1 2 3; f:{[x] x[0]+x[1]}; f a + 5"  "13"
test_case "rtl_fn_scalar_op"         "f:{[x] x*2}; f 3 + 1"                  "8"
test_case "rtl_chain_kw"             "count til 5"                            "5"
test_case "generic_bracket_args_parse" "f[1;2]" "Error: undefined variable 'f'"
test_case_multiline "nested_lists" "a:(1;2); b:(a;a); b" $'1 2\n1 2'
test_case "dict_lookup" "d:(1;2;3)!(4;5;6); d[3]" "6"
test_case "dict_lookup_implicit_apply" "d:(1;2;3)!(4;5;6); d 3" "6"
test_case "dict_keys" "d:(1;2;3)!(4;5;6); key[d]" "1 2 3"
test_case "dict_values" "d:(1;2;3)!(4;5;6); value[d]" "4 5 6"
test_case "dict_add_scalar" "d:2 + ((1;2;3)!(4;5;6)); d[3]" "8"
test_case "dict_type" "type ((1;2)!(4;5))" "\`dict"
test_case "dict_string_lookup" "d:\"abc\"!\"def\"; d[(first \"b\")]" "'e'"
test_case "dict_string_lookup_implicit_apply" "d:\"abc\"!\"def\"; d first \"b\"" "'e'"
test_case "dict_string_key" "key[\"abc\"!\"def\"]" "\"abc\""
test_case "dict_string_value" "value[\"abc\"!\"def\"]" "\"def\""

# Parse exposure
test_case_multiline "parse_bracket_form" "parse[\"2+2\"]" $'+\n2\n2'
test_case "eval_parse_chain" "eval[parse[\"2+3\"]]" "5"
test_case "eval_comma_list" "eval[parse[\"1,2,3\"]]" "1 2 3"
test_case_multiline "parse_keyword_form" "parse \"sum a + 5\"" $'sum\n(+;`a;5)'
test_case "symbol_literal" "\`foo" "\`foo"
test_case "function_literal_print" "{1;2;3}" "{1;2;3}"
test_case "function_literal_type" "type {1;2}" "\`function"
test_case "function_with_params_print" "{[a;b] a+b}" "{[a;b] a+b}"
test_case "function_with_single_param" "{[x] x*2}" "{[x] x*2}"
test_case "function_with_three_params" "{[a;b;c] a+b+c}" "{[a;b;c] a+b+c}"
test_case "function_call_two_args" "f:{[a;b] a+b}; f[2;3]" "5"
test_case "function_call_uses_global" "g:10; f:{[a] a+g}; f[2]" "12"
test_case "function_call_local_shadowing" "x:100; f:{[x] x+1}; f[2]" "3"
test_case "function_local_assignment_not_global" "f:{[a] b:a+1; b}; f[2]; b" "Error: undefined variable 'b'"
test_case "function_no_outer_local_capture" "outer:{[x] inner:{[y] x+y}; inner[3]}; outer[4]" "Error: undefined variable 'x'"
test_case "parse_symbol_literal_wrapper" "parse \"\`foo\"" "\`foo"
test_case_multiline "parse_symbols" "parse \"foo + bar\"" $'+\n`foo\n`bar'
test_case_multiline "parse_dict" "parse \"(1;2)!(4;5)\"" $'!\n(enlist;1;2)\n(enlist;4;5)'
test_case_multiline "lex_simple_arithmetic" "lex \"1 + 2\"" $'"1"\n"+"\n"2"'
test_case_multiline "lex_function_call" "lex \"f[x]\"" $'"f"\n"["\n"x"\n"]"'
test_case_multiline "lex_keyword" "lex \"sum a\"" $'"sum"\n"a"'
test_case "lex_empty_string" "lex \"\"" ""
test_case "lex_float" "lex \"3.14\"" "\"3.14\""
test_case "lex_symbol" "lex \"\`foo\"" "\"\`foo\""
test_case_multiline "lex_empty_symbol_plus" "lex \"\`+\"" $'"`"\n"+"'
test_case_multiline "lex_empty_symbol_amp" "lex \"\`&\"" $'"`"\n"&"'
test_case_multiline "lex_empty_symbol_semicolon" "lex \"\`;\"" $'"`"\n";"'
test_case "lex_dotted_symbol" "lex \"\`a.b\""         "\"\`a.b\""
test_case "lex_nested_dotted_symbol" "lex \"\`a.b.c\"" "\"\`a.b.c\""
test_case "dotted_symbol_equality" "\`a.b=\`a.b"     "1b"
test_case "dotted_symbol_inequality" "\`a.b=\`a.c"    "0b"
test_case "dotted_symbol_type" "type \`a.b"           "\`symbol"
test_case "parse_generic_call_head_type" "type first parse\"f[2;3]\"" "\`symbol"
test_case_multiline "parse_projection_holes" "parse \"f[;x;;y]\"" $'`f\nP\n`x\nP\n`y'
test_case "parse_unclosed_paren_errors"   "parse \" (\""  "Error: parse: failed to parse expression"
test_case "parse_stray_bracket_errors"    "parse \"[\""   "Error: parse: failed to parse expression"
test_case "parse_trailing_operator_errors" "parse \"1+\"" "Error: parse: failed to parse expression"
test_case "parse_unclosed_brace_errors"   "parse \"{\""   "Error: parse: failed to parse expression"
test_case "parse_stray_tilde_errors"      "parse \"~\""   "Error: parse: failed to parse expression"
test_case "projection_partial_apply" "f:{[x;y] x+y}; g:f[2;]; g[3]" "5"
test_case "projection_print" "f:{[x;y;z]x+y+z}; g:f[2;;3]; g" "{[x;y;z]x+y+z}[2;;3]"
test_case "projection_type" "f:{[x;y] x+y}; type f[2;]" "\`projection"

# Each operator
test_case "each_first_on_list" "first' (\"cat\";\"dog\")" "\"cd\""
test_case "each_last_on_list" "last' (\"cat\";\"dog\")" "\"tg\""
test_case "each_count_on_list" "count' (\"cat\";\"dog\")" "3 3"
test_case_multiline "each_til_on_multi" "til' 3 4 5" $'0 1 2\n0 1 2 3\n0 1 2 3 4'
test_case "each_func_alias" "f:first'; f (\"cat\";\"dog\")" "\"cd\""
test_case "each_type" "type first'" "\`adverbfunc"

# Adverb (') as first-class value
test_case "adverb_standalone" "'" "'"
test_case "adverb_type_bracket" "type[']" "\`adverb"
test_case "adverb_type_at" "type@'" "\`adverb"
test_case "adverb_equality" "'='" "1b"
test_case "adverb_assignment" "x:';x" "'"
test_case "adverb_assignment_type" "x:';type x" "\`adverb"
test_case "adverb_apply_to_func" "x:';x first" "first'"
test_case "adverb_apply_and_call" "x:'; (x first) (\"cat\";\"dog\")" "\"cd\""
test_case "adverb_parse_roundtrip" "eval[parse[\"'\"]]" "'"
test_case "adverb_greedy_postfix" "(type) '" "type'"
test_case "adverb_func_alias" "f:first'; f (1 2 3;4 5 6)" "1 4"

# Each with multi-argument functions
test_case "each_multiarg_two_vecs" "f:{[x;y] x+y}; f'[1 2 3;4 5 6]" "5 7 9"
test_case "each_multiarg_scalar_vec" "f:{[x;y] x+y}; f'[10;1 2 3]" "11 12 13"
test_case "each_multiarg_vec_scalar" "f:{[x;y] x**y}; f'[2 3 4;3]" "8 27 64f"
test_case "each_multiarg_nested" "f:{[x;y] first[x]+last[y]}; f'[(1 2;3 4);(5 6;7 8)]" "7 11"
test_case "each_multiarg_len_mismatch" "f:{[x;y] x+y}; f'[1 2;3 4 5]" "Error: each: vector length mismatch"
test_case "each_multiarg_list_list" "f:{[x;y] sum x+y}; f'[(1 2;3 4);(5 6;7 8)]" "14 22"
test_case "each_multiarg_three" "f:{[x;y;z] x+y+z}; f'[1 2;3 4;5 6]" "9 12"
test_case "each_multiarg_projector" "f:{[x;y;z] x+y*z}; (f'[1 2 3;4 5 6;])[7]" "29 37 45"
test_case "each_multiarg_partial" "f:{[x;y;z] x+y+z}; (f'[1;2])[3]" "6"
test_case "each_multiarg_too_many" "f:{[x;y] x+y}; f'[1;2;3]" "Error: function expects 2 args, got 3"
test_case "each_multiarg_projector_no_vec" "f:{[x;y] x * y}; (f'[2;])[3]" "6"

# Symbol vector literal (`a`b`c)
test_case "sym_vec_literal_two" "\`a\`b" "\`a\`b"
test_case "sym_vec_literal_three" "\`a\`b\`c" "\`a\`b\`c"
test_case "sym_vec_literal_with_spaces" "\`a \`b \`c" "\`a\`b\`c"
test_case "sym_vec_literal_type" "type \`a\`b\`c" "\`SYMBOL"
test_case "sym_vec_literal_index" "\`a\`b\`c[0]" "\`a"
test_case "sym_vec_literal_index_last" "\`a\`b\`c[2]" "\`c"
test_case "sym_vec_literal_index_multi" "\`a\`b\`c[0 1]" "\`a\`b"
test_case "sym_vec_literal_equality" "\`a\`b=\`a\`b" "11b"
test_case "sym_vec_literal_inequality" "\`a\`b=\`a\`c" "10b"
test_case "sym_vec_literal_join" "\`a\`b,\`c\`d" "\`a\`b\`c\`d"
test_case "sym_vec_literal_in_parens" "(\`a\`b\`c)" "\`a\`b\`c"
test_case "sym_vec_literal_first" "first \`a\`b\`c" "\`a"
test_case "sym_vec_literal_last" "last \`a\`b\`c" "\`c"
test_case "sym_vec_literal_count" "count \`a\`b\`c" "3"
test_case "sym_vec_literal_infix" "\`a\`b\`c,\`d" "\`a\`b\`c\`d"
test_case "sym_vec_literal_parse" "parse \"\`a\`b\`c\"" "\`a\`b\`c"
test_case "sym_vec_literal_eval_parse" "eval parse \"\`a\`b\`c\"" "\`a\`b\`c"
test_case "sym_vec_literal_empty_in_vec" "\`\`a\`b" "\`\`a\`b"

# Type builtin
test_case "type_int" "type 2" "\`long"
test_case "type_default_long_literal" "type 5" "\`long"
test_case "default_long_literal_print" "5" "5"
test_case "default_long_equals_j_suffix" "5 = 5j" "1b"
test_case "single_long_suffix" "1j" "1"
test_case "single_int_suffix" "1i" "1i"
test_case "single_short_suffix" "1h" "1h"
test_case "long_null_literal" "0Nj" "0N"
test_case "long_null_default_literal" "0N" "0N"
test_case "long_inf_literal" "0Wj" "0W"
test_case "long_inf_default_literal" "0W" "0W"
test_case "long_negative_inf_literal" "-0Wj" "-0W"
test_case "long_negative_inf_default_literal" "-0W" "-0W"
test_case "int_null_literal" "0Ni" "0Ni"
test_case "int_inf_literal" "0Wi" "0Wi"
test_case "int_negative_inf_literal" "-0Wi" "-0Wi"
test_case "float_null_literal" "0Nf" "0Nf"
test_case "float_inf_literal" "0Wf" "0Wf"
test_case "float_negative_inf_literal" "-0Wf" "-0Wf"
test_case "default_null_is_long" "0N = 0Nj" "1b"
test_case "null_in_number_sequence" "1 0N 2" "1 0N 2"
test_case "null_in_float_sequence" "0N 2.5f" "0N 2.5f"
test_case "inf_in_number_sequence" "0W -1 0" "0W -1 0"
test_case "neginf_in_number_sequence" "-0W -1 0" "-0W -1 0"
test_case "suffix_on_null_in_sequence" "1 0Ni 2" "Error: Failed to parse expression"
test_case "suffix_on_null_float_sequence" "0Nf 2.5" "Error: Failed to parse expression"
test_case "explicit_suffix_on_null_in_sequence" "1 0Nj 3" "Error: Failed to parse expression"
test_case "type_int_vector" "type (1;2)" "\`LONG"
test_case "type_float" "type 2.0" "\`float"
test_case "type_string" "type \"str\"" "\`CHAR"
test_case "type_symbol" "type \`hello" "\`symbol"
test_case "type_symbol_vector" "type (\`a;\`b)" "\`SYMBOL"
test_case "type_nested_builtin" "type type 1" "\`symbol"
test_case "type_builtin" "type sum" "\`builtin"

# Float suffix parsing
test_case "float_suffix_simple" "1f" "1f"
test_case "float_suffix_decimal" "1.5f" "1.5f"
test_case "float_suffix_type" "type 1f" "\`float"
test_case "float_suffix_vector" "1 2.5 3f" "1 2.5 3f"
test_case "float_suffix_vector_type" "type (1 2.5 3f)" "\`FLOAT"
test_case "float_suffix_arithmetic" "1f + 2f" "3f"
test_case "float_suffix_multiply" "1.5f * 2" "3f"
test_case "float_suffix_mixed_with_decimal" "1 2.5 3f" "1 2.5 3f"

# Suffix parsing rules
test_case "suffix_only_last_int" "1 2 3i" "1 2 3i"
test_case "suffix_only_last_short" "1 2 3h" "1 2 3h"
test_case "suffix_none_defaults_long" "1 2 3" "1 2 3"
test_case "suffix_invalid_first" "1i 2 3" "Error: Failed to parse expression"
test_case "suffix_invalid_middle" "1 2i 3" "Error: Failed to parse expression"
test_case "suffix_invalid_multiple" "1i 2h 3j" "Error: Failed to parse expression"
test_case "suffix_invalid_all_suffixed_ints" "1i 2i 3i" "Error: Failed to parse expression"
test_case "suffix_invalid_mixed_shorts" "1h 2h 3h" "Error: Failed to parse expression"
test_case "suffix_invalid_mixed_order" "1j 2i 3h" "Error: Failed to parse expression"

# Short null/inf sentinel literals
test_case "short_null_scalar"           "0Nh"               "0Nh"
test_case "short_inf_scalar"            "0Wh"               "0Wh"
test_case "short_neginf_scalar"         "-0Wh"              "-0Wh"
test_case "short_null_equals_null"      "0Nh=0Nh"           "1b"
test_case "short_null_type"             "type 0Nh"          "\`short"
test_case "short_null_is_null"          "null 0Nh"          "1b"
test_case "short_inf_type"              "type 0Wh"          "\`short"
test_case "short_all_null_vec_suffix"   "0N 0N 0Nh"         "0N 0N 0Nh"
test_case "short_null_vec_cast"         "\"h\"\$ (1,0N,3)"  "1 0N 3h"

# Int null sentinel literals
test_case "int_all_null_vec_suffix"     "0N 0N 0Ni"         "0N 0N 0Ni"
test_case "int_null_vec_cast"           "\"i\"\$ (1,0N,3)"  "1 0N 3i"

# Long null sentinel literals
test_case "long_null_scalar"            "0N"                "0N"
test_case "long_null_eq_null"           "0N=0N"             "1b"
test_case "long_null_vec_suffix"        "0N 0N 0N"          "0N 0N 0N"
test_case "long_null_mixed_numeric"     "0N 5 0N"           "0N 5 0N"
test_case "long_null_vec_cast"          "\"j\"\$ (1,0N,3)"  "1 0N 3"

# Null type
test_case "trailing_semicolon_is_null" "2+3;" ""
test_case "parse_empty_string_is_null" "parse[\"\"]" ""
test_case "type_of_empty_input" "type[parse[\"\"]]" "\`list"

# Single-element list unwrapping
test_case "bare_builtin_print" "print" "print"
test_case "bare_builtin_sum" "sum" "sum"
test_case "bare_builtin_count" "count" "count"
test_case "single_element_list_int" "(42)" "42"
test_case "single_element_list_float" "(3.14)" "3.14f"
test_case "single_element_list_string" "(\"hello\")" "\"hello\""
test_case "single_element_list_symbol" "(\`foo)" "\`foo"
test_case "single_element_list_symbol_eval" "s:\`test; (s)" "\`test"
test_case "single_element_list_function" "f:{[x] x+1}; (f)" "{[x] x+1}"
test_case "single_element_list_char_vector" "(\"abc\")" "\"abc\""
test_case "nested_single_element_unwrap" "a:1; ((a))" "1"

# Boolean literals and vectors
test_case "bool_zero" "0b" "0b"
test_case "bool_one" "1b" "1b"
test_case "bool_vector" "1010011b" "1010011b"
test_case "bool_vector_complex" "10101010b" "10101010b"
test_case "count_bool_vector" "count 1010b" "4"
test_case "type_bool_scalar" "type 1b" "\`bool"
test_case "type_bool_vector" "type 1010011b" "\`BOOL"
test_case "byte_scalar_literal" "0x01" "0x01"
test_case "byte_scalar_literal_odd_digits" "0x1" "0x01"
test_case "byte_vector_literal" "0x0102" "0x0102"
test_case "byte_vector_literal_odd_digits" "0x102" "0x0102"
test_case "type_byte_scalar" "type 0x7f" "\`byte"
test_case "type_byte_vector" "type 0x010203" "\`BYTE"

# ── Byte arithmetic / promotion ───────────────────────────────────────
test_case "byte_plus_byte"          "0x01 + 0x02"        "0x03"
test_case "byte_plus_long"          "0x01 + 42"          "43"
test_case "byte_mul_byte"           "0x05 * 0x03"        "15"
test_case "byte_div"                "0x0a % 3"           "3.33333f"
test_case "byte_less_true"          "0x01 < 0x02"        "1b"
test_case "byte_less_false"         "0x02 < 0x01"        "0b"
test_case "byte_greater_true"       "0x0a > 0x05"        "1b"
test_case "byte_lte_equal"          "0x03 <= 0x03"       "1b"
test_case "byte_gte_equal"          "0x03 >= 0x03"       "1b"
test_case "byte_plus_atom_vec"      "0x01 + 0x010203"    "0x020304"
test_case "byte_vec_plus_vec"       "0x0101 + 0x0203"    "0x0304"
test_case "byte_vec_mul_vec"        "0x0102 * 0x0203"    "2 6"
test_case "byte_vec_less_vec"       "0x0103 < 0x0202"    "10b"
test_case "not_byte_scalar"         "not 0x00"           "1b"
test_case "not_byte_nonzero"        "not 0x7f"           "0b"
test_case "not_byte_vec"            "not 0x000102"       "100b"
test_case "neg_byte_zero"           "neg[0x00]"          "0i"
test_case "neg_byte_pos"            "neg[0x05]"          "-5i"
test_case "neg_byte_vec"            "neg[0x000102]"      "0 -1 -2i"
test_case "byte_pow"                "0x02 ** 3"          "8f"

test_case "ser_byte" "ser 0x01" "0x0a0001"
test_case "ser_char" "ser first \"c\"" "0x050063"
test_case "ser_char_vec_no_trailing_nul" "ser \"cd\"" "0x150002000000000000006364"
test_case "drop_serialized_char_vec" "1 _ ser \"cd\"" "0x0002000000000000006364"
test_case "ser_long_vec_promoted" "ser (1;2)" "0x1300020000000000000001000000000000000200000000000000"
test_case "ser_list_mixed" "ser (1;\"a\")" "0x20000200000000000000030001000000000000001500010000000000000061"
test_case "deser_list_type" "type deser ser (1;\"a\")" "\`list"
test_case "deser_list_count" "count deser ser (1;\"a\")" "2"
test_case "deser_list_nested" "type deser ser ((1;2f);(3;4f))" "\`list"
test_case "deser_long" "deser ser 42" "42"
test_case "deser_int" "deser ser 42i" "42i"
test_case "deser_short" "deser ser 42h" "42h"
test_case "deser_float" "deser ser 3.14f" "3.14f"
test_case "deser_char" "deser ser first \"c\"" "'c'"
test_case "deser_byte" "deser ser 0x01" "0x01"
test_case "deser_bool_vec" "deser ser 10b" "10b"
test_case "deser_long_vec" "deser ser 1 2 3" "1 2 3"
test_case "deser_symbol" "deser ser \`abc" "\`abc"
test_case "ser_symbol" "ser \`abc" "0x070061626300"
test_case "deser_symbol_empty" "deser ser \`" "\`"
test_case "deser_symbol_underscore" "type deser ser \`_XY" "\`symbol"
test_case "deser_string" "deser ser \"hello\"" "\"hello\""
test_case "deser_not_byte_vec" "deser 42" "Error: deser expects a byte vector"
test_case "ser_sym_vec" "ser \`a\`b\`c" "0x17000300000000000000610062006300"
test_case "deser_sym_vec" "deser ser \`a\`b\`c" "\`a\`b\`c"
test_case "deser_sym_vec_empty_sym" "deser ser \`" "\`"
test_case "deser_sym_vec_underscore" "deser ser \`_x\`y2" "\`_x\`y2"
test_case "ipc_sym_vec" "ser deser ser \`foo\`bar" "0x17000200000000000000666f6f0062617200"
test_case "deser_dict_type" "type deser ser \`a\`b!1 2" "\`dict"
test_case "deser_dict_count" "count deser ser \`a\`b!1 2" "2"
test_case "deser_dict_key" "key deser ser \`a\`b!1 2" "\`a\`b"
test_case "deser_dict_value" "value deser ser \`a\`b!1 2" "1 2"
test_case "deser_dict_empty" "type deser ser ()!()" "\`dict"
test_case "deser_func_type" "type deser ser {x}" "\`function"
test_case "deser_func_eval" "deser ser {[x] x+1}@5" "6"
test_case "ser_func" "ser {[x] x+1}" "0x22007b5b785d20782b317d00"
test_case "parse_bool" "parse\"1b\"" "1b"
test_case "parse_bool_vector" "parse\"1010011b\"" "1010011b"
test_case "bool_in_list" "(0b; 1b; 1b; 0b)" "0110b"
test_case "bool_assignment" "b:10110b; b" "10110b"

# @ operator (apply left to right as a single argument: f@x = f[x])
test_case "at_fn_scalar"            "{[x] x*2}@5"                               "10"
test_case "at_fn_vector_result"     "{[x] x+1}@1 2 3"                           "2 3 4"
test_case "at_fn_sum_arg"           "{[x] sum x}@1 2 3"                         "6"
test_case "at_chained"              "{[x] x+1}@{[x] x*2}@3"                     "7"
test_case "at_variable"             "f:{[x] x*x}; f@4"                          "16"
test_case "at_vec_index"            "(10 20 30 40)@2"                           "30"
test_case "at_builtin_alias"        "f:sum; f@1 2 3"                            "6"
test_case "at_with_list_arg"        "{[x] count x}@(1 2 3)"                     "3"
test_case "at_in_expression"        "2*{[x] x+3}@4"                             "14"
test_case "at_identity"             "{[x] x}@42"                                "42"
test_case "at_two_calls"            "f:{[x] x+1}; f@f@f@1"                      "4"
test_case "at_projection_fill"      "f:{[x;y] x+y}; f[1;]@3"                    "4"
test_case "at_nested_result"        "{[x] x*2}@{[x] x+10}@5"                    "30"
test_case "at_callable_form"        "@[{[x] x-1};8]"                            "7"
test_case "at_on_dict"              "d:(0 1 2)!(10 20 30); d@1"                 "20"

# . operator (apply left to right, spreading list elements as args: f.(x;y;z)=f[x;y;z])
test_case "dot_two_args"            "{[x;y] x+y} . (3;4)"                       "7"
test_case "dot_three_args"          "{[x;y;z] x+y+z} . (1;2;3)"                 "6"
test_case "dot_variable_fn"         "f:{[x;y] x*y}; f . (3;7)"                  "21"
test_case "dot_non_list_rhs"        "{[x] x+10} . 5"                            "15"
test_case "dot_enlist_rhs"          "{[x] x*2} . enlist 5"                      "10"
test_case "dot_vec_args"            "{[x;y] x+y} . (1 2 3;10 20 30)"            "11 22 33"
test_case "dot_subtraction"         "{[x;y] x-y} . (10;3)"                      "7"
test_case "dot_join"                "{[x;y] x,y} . (1 2;3 4)"                   "1 2 3 4"
test_case "dot_composed_args"       "{[x;y] x+y} . ({[x] x*2}@3;{[x] x+1}@4)"   "11"
test_case "dot_four_args"           "{[a;b;c;d] a+b+c+d} . (1;2;3;4)"           "10"
test_case "dot_callable_form"       ".[{[x;y] x*y};(3;5)]"                      "15"
test_case "dot_single_via_enlist"   "f:{[x] x*x}; f . enlist 9"                 "81"
test_case "dot_mixed_types"         "{[x;y] x+y} . (2.5;1.5)"                   "4f"

# | and & operators (greater/lesser)
test_case "pipe_long_scalar"         "4|5"                                      "5"
test_case "amp_long_scalar"          "4&5"                                      "4"
test_case "pipe_bool_scalar"         "1b|0b"                                    "1b"
test_case "amp_bool_scalar"          "1b&0b"                                    "0b"
test_case "pipe_char_scalar" "(first \"a\")|(first \"z\")" "'z'"
test_case "amp_char_scalar" "(first \"a\")&(first \"z\")" "'a'"
test_case "pipe_float_scalar"        "2.5|4"                                    "4"
test_case "amp_float_scalar"         "2.5&4"                                    "2.5f"
test_case "pipe_vector_vector"       "1 5 3 | 2 4 9"                            "2 5 9"
test_case "amp_vector_vector"        "1 5 3 & 2 4 9"                            "1 4 3"
test_case "pipe_vector_scalar"       "1 5 3 | 4"                                "4 5 4"
test_case "amp_scalar_vector"        "4 & 1 5 3"                                "1 4 3"
test_case "pipe_bool_vectors"        "1010b | 0101b"                            "1111b"
test_case "amp_bool_vectors"         "1010b & 0101b"                            "0000b"
test_case "pipe_char_vectors"        "\"az\" | \"by\""                          "\"bz\""
test_case "amp_char_vectors"         "\"az\" & \"by\""                          "\"ay\""
test_case "pipe_callable_form"       "|[2;9]"                                   "9"
test_case "amp_callable_form"        "&[2;9]"                                   "2"
test_case "pipe_in_expression"       "10+2|9"                                   "19"
test_case "amp_in_expression"        "10+2&9"                                   "12"
test_case "pipe_negatives"           "-5|-2"                                    "-2"
test_case "amp_negatives"            "-5&-2"                                    "-5"
test_case "pipe_len_mismatch"        "1 2|3 4 5"                                "Error: cannot compare vectors of different lengths"
test_case "amp_non_numeric"          "\`a & 2"                                  "Error: operator requires int/float/bool/char operands"

# ── Fill operator (^) ───────────────────────────────────────────────────
test_case "fill_scalar_not_null"    "0N^5"                    "5"
test_case "fill_scalar_null"        "1^0N"                    "1"
test_case "fill_scalar_both_null"   "0N^0N"                   "0N"
test_case "fill_scalar_int_null"    "1^0Ni"                   "1"
test_case "fill_scalar_float_null"  "1.0^0Nf"                 "1f"
test_case "fill_scalar_null_left"   "0N^1"                    "1"
test_case "fill_vec_fill_one"       "1^ 4 0N 6"               "4 1 6"
test_case "fill_vec_no_nulls"       "1 2 3^ 4 5 6"            "4 5 6"
test_case "fill_vec_fill_two"       "10 20 30^ 1 0N 3"        "1 20 3"
test_case "fill_vec_all_nulls"      "10 20 30^ 0N 0N 0N"      "10 20 30"
test_case "fill_vec_first_null"     "99^ 0N 2 3"              "99 2 3"
test_case "fill_vec_len_mismatch"   "1 2^ 0N 0N 0N"           "Error: fill: vectors must be same length"
test_case "fill_right_assoc"        "10 20 30^ 1 0N 3"        "1 20 3"
test_case "fill_callable"           "^[1 2 3;4 0N 6]"         "4 2 6"

# Find keyword
test_case "find_scalar_found"       "find[3 4 5;4]"           "1"
test_case "find_scalar_notfound"    "find[3 4 5;6]"           "3"
test_case "find_vector_found"       "find[3 4 5;4 5]"         "1 2"
test_case "find_vector_notfound"    "find[3 4 5;3 6]"         "0 3"
test_case "find_string_in_list"     "find[(\"cat\";\"dog\");\"dog\"]" "1"
test_case "find_multi_str_in_list"  "find[(\"cat\";\"dog\";\"bird\");(\"bird\";\"cat\")]" "2 0"
test_case "find_char_in_str"        "find[\"cat\";first \"a\"]" "1"
test_case "find_type"               "type find"               "\`builtin"

# Find operator (?)
test_case "find_op_scalar_found"     "3 4 5 ? 4"            "1"
test_case "find_op_scalar_notfound"  "3 4 5 ? 6"            "3"
test_case "find_op_vector_found"     "3 4 5 ? 4 5"          "1 2"
test_case "find_op_vector_notfound"  "3 4 5 ? 3 6"          "0 3"

# ── Random number generation (?) ───────────────────────────────────────
# Type checks
test_case "rand_type_long"       "type 5 ? 0j"              "\`LONG"
test_case "rand_type_int"        "type 5 ? 0i"              "\`INT"
test_case "rand_type_short"      "type 5 ? 0h"              "\`SHORT"
test_case "rand_type_byte"       "type 5 ? 0x00"            "\`BYTE"
test_case "rand_type_bool"       "type 5 ? 0b"              "\`BOOL"
test_case "rand_type_float"      "type 5 ? 1.0f"            "\`FLOAT"
test_case "rand_type_default"    "type 5 ? 0"               "\`LONG"
# Count checks
test_case "rand_count"           "count (10 ? 0j)"          "10"
test_case "rand_count_empty"     "count (0 ? 0j)"           "0"
# Scalar result for n=1
test_case "rand_scalar_type"     "type 1 ? 100"             "\`long"
# Bounded range (values in [0, bound))
test_case "rand_bounded_max"     "max[(1000 ? 10)] < 10"    "1b"
test_case "rand_bounded_short"   "max[(500 ? 5h)] < 5h"     "1b"
# Zero RHS for float → zeros
test_case "rand_zero_float"      "max[(5 ? 0f)]"            "0f"
# Bracket form with scalar count
test_case "rand_bracket_form"    "type ?[3;0j]"             "\`LONG"
# Seed determinism
test_case "rand_seed_deterministic" "sys.seed: 12345; a:5?100; sys.seed: 0; 1?0; sys.seed: 12345; a=5?100" "11111b"
# Seed readable via sys.seed
test_case "rand_seed_type"       "type sys.seed"            "\`long"

# ── neg keyword ────────────────────────────────────────────────────────
test_case "neg_pos_long"       "neg[5]"                "-5"
test_case "neg_neg_long"       "neg[-5]"               "5"
test_case "neg_zero_long"      "neg[0]"                "0"
test_case "neg_pos_int"        "neg[5i]"               "-5i"
test_case "neg_neg_int"        "neg[-5i]"              "5i"
test_case "neg_pos_short"      "neg[5h]"               "-5h"
test_case "neg_neg_short"      "neg[-5h]"              "5h"
test_case "neg_pos_float"      "neg[5.5f]"             "-5.5f"
test_case "neg_neg_float"      "neg[-3.25f]"           "3.25f"
test_case "neg_zero_float"     "neg[0f]"               "-0f"
test_case "neg_type_long"      "type neg[5]"           "\`long"
test_case "neg_type_int"       "type neg[5i]"          "\`int"
test_case "neg_type_short"     "type neg[5h]"          "\`short"
test_case "neg_type_float"     "type neg[5.5f]"        "\`float"
# Vectors
test_case "neg_long_vec"       "neg[1 2 3]"            "-1 -2 -3"
test_case "neg_int_vec"        "neg[1 2 3i]"           "-1 -2 -3i"
test_case "neg_short_vec"      "neg[1 2 3h]"           "-1 -2 -3h"
test_case "neg_float_vec"      "neg[1.5 2.5 3.5f]"     "-1.5 -2.5 -3.5f"
test_case "neg_neg_vec"        "neg[-1 -2 -3]"         "1 2 3"
test_case "neg_mixed_vec"      "neg[0 -5 3]"           "0 5 -3"
test_case "neg_vec_type_eq"    "type neg[1 2 3i]"      "\`INT"
# Lists
test_case "neg_list"           "neg[(1;-2;3)]"         "-1 2 -3"
# Mixed list prints multi-line at top-level; test via enlist to force single-line
test_case "neg_list_mixed"     "enlist neg[(1;-2i;3.5f)]"  "(-1;2i;-3.5f)"
test_case "neg_empty_list"     "count neg[()]"              "0"
# Nulls
test_case "neg_null_long"      "neg[0N]"               "0N"
test_case "neg_null_int"       "neg[0Ni]"              "0Ni"
test_case "neg_null_float"     "neg[0Nf]"              "0Nf"
test_case "neg_null_vec"       "neg[0N 5 0N]"          "0N -5 0N"
test_case "neg_type_null_float" "type neg[0Nf]"          "\`float"
# Inf
test_case "neg_inf_long"       "neg[0W]"               "-0W"
test_case "neg_neginf_long"    "neg[-0W]"              "0W"
test_case "neg_inf_int"        "neg[0Wi]"              "-0Wi"
test_case "neg_neginf_int"     "neg[-0Wi]"             "0Wi"
test_case "neg_inf_float"      "neg[0Wf]"              "-0Wf"
test_case "neg_neginf_float"   "neg[-0Wf]"             "0Wf"
# Bracket/keyword form
test_case "neg_bracket"        "neg[10]"               "-10"
test_case "neg_type_builtin"   "type neg"              "\`builtin"

# ── distinct keyword ───────────────────────────────────────────────────
test_case "distinct_long_vec"         "distinct[1 2 3 1 2]"         "1 2 3"
test_case "distinct_long_vec_nodup"   "distinct[1 2 3]"             "1 2 3"
test_case "distinct_long_vec_all_same" "distinct[5 5 5 5]"          "5"
test_case "distinct_int_vec"          "distinct[1 2 1i]"            "1 2i"
test_case "distinct_short_vec"        "distinct[1 2 1h]"            "1 2h"
test_case "distinct_float_vec"        "distinct[1.5 2.5 1.5f]"      "1.5 2.5f"
test_case "distinct_bool_vec"         "distinct[1011b]"             "10b"
test_case "distinct_byte_vec"         "distinct[enlist[0x01;0x02;0x01]]"    "0x0102"
test_case "distinct_char_vec"         "distinct[\"hello\"]"          "\"helo\""
test_case "distinct_sym_vec"          "distinct[\`a\`b\`a\`c]"      "\`a\`b\`c"
test_case "distinct_list"             "distinct[(1;2;3;1;2)]"      "1 2 3"
test_case "distinct_list_mixed"       "enlist distinct[(1;2i;1;3f)]" "(1;2i;3f)"
test_case "distinct_empty_vec"        "distinct[()]"               ""
test_case "distinct_empty_long"       "count distinct[0#0]"        "0"
test_case "distinct_type_preserved"   "type distinct[1 2 3i]"      "\`INT"
test_case "distinct_type_builtin"     "type distinct"              "\`builtin"
test_case "distinct_null_in_vec"      "distinct[0N 5 0N 5]"        "0N 5"
test_case "distinct_inf"              "distinct[0W -0W 0W]"        "0W -0W"

# ── reverse keyword ────────────────────────────────────────────────────
test_case "reverse_long_vec"     "reverse[1 2 3]"            "3 2 1"
test_case "reverse_int_vec"      "reverse[1 2 3i]"           "3 2 1i"
test_case "reverse_short_vec"    "reverse[1 2 3h]"           "3 2 1h"
test_case "reverse_float_vec"    "reverse[1.5 2.5 3.5f]"     "3.5 2.5 1.5f"
test_case "reverse_bool_vec"     "reverse[1011b]"            "1101b"
test_case "reverse_byte_vec"     "reverse[0x010203]"         "0x030201"
test_case "reverse_char_vec"     "reverse[\"hello\"]"         "\"olleh\""
test_case "reverse_list"         "enlist reverse[(1;2i;\"a\")]" "(\"a\";2i;1)"
test_case "reverse_scalar"       "reverse 42"                "42"
test_case "reverse_empty"        "count reverse[()]"         "0"
test_case "reverse_single"       "reverse[enlist[42]]"       "42"
test_case "reverse_type_vec"     "type reverse[1 2 3]"       "\`LONG"
test_case "reverse_type_list"    "type reverse[(1;2i)]"      "\`list"
test_case "reverse_type_builtin" "type reverse"              "\`builtin"

# Bracket-form operator parsing: expr OP[args] should be expr applied
# to OP[args], not a binary op.  Test all expression operators.
test_case "bracket_plus"               "type +[3;4]"              "\`long"
test_case "bracket_minus"              "type -[10;3]"             "\`long"
test_case "bracket_star"               "type *[2;3]"              "\`long"
test_case "bracket_bang"               "type ![\`a\`b;1 2]"       "\`dict"
test_case "bracket_comma"              "type ,[1;2]"              "\`LONG"
test_case "bracket_hash"               "type #[3;1 2 3]"          "\`LONG"
test_case "bracket_underscore"         "type _[1;1 2 3]"          "\`LONG"
test_case "bracket_equal"              "type =[1;1]"              "\`bool"
test_case "bracket_less"               "type <[1;2]"              "\`bool"
test_case "bracket_greater"            "type >[2;1]"              "\`bool"
test_case "bracket_amp"                "type &[2;9]"              "\`long"
test_case "bracket_pipe"               "type |[2;9]"              "\`long"
test_case "bracket_dollar"             "type \$[\`float;42]"      "\`float"
test_case "bracket_at"                 "type @[;1] 3 4 5"         "\`long"
test_case "bracket_dot"                "type .[+;(3;5)]"          "\`long"
test_case "bracket_question"           "type ?[1 2 3; 2]"         "\`long"

# IPC tests (same-process via localhost)
IPC_PORT=19101
test_case "ipc_same_process_int"     "listen $IPC_PORT; h: hopen[\"127.0.0.1\";$IPC_PORT]; h[42]" "42"
IPC_PORT=19102
test_case "ipc_same_process_float"   "listen $IPC_PORT; h: hopen[\"127.0.0.1\";$IPC_PORT]; h[3.14]" "3.14f"
IPC_PORT=19103
test_case "ipc_same_process_string"  "listen $IPC_PORT; h: hopen[\"127.0.0.1\";$IPC_PORT]; h[\"hello\"]" "\"hello\""
IPC_PORT=19104
test_case "ipc_same_process_symbol"  "listen $IPC_PORT; h: hopen[\"127.0.0.1\";$IPC_PORT]; h[\`abc]" "\`abc"
IPC_PORT=19105
test_case "ipc_same_process_bool"    "listen $IPC_PORT; h: hopen[\"127.0.0.1\";$IPC_PORT]; h[1b]" "1b"
IPC_PORT=19106
test_case "ipc_same_process_vec"     "listen $IPC_PORT; h: hopen[\"127.0.0.1\";$IPC_PORT]; h[1 2 3]" "1 2 3"
IPC_PORT=19107
test_case "ipc_hclose"               "listen $IPC_PORT; h: hopen[\"127.0.0.1\";$IPC_PORT]; hclose h" ""
IPC_PORT=19108
test_case "ipc_same_process_ser_roundtrip" "listen $IPC_PORT; h: hopen[\"127.0.0.1\";$IPC_PORT]; ser h[42]" "0x03002a00000000000000"
IPC_PORT=19109
test_case "ipc_sym_vec_roundtrip" "listen $IPC_PORT; h: hopen[\"127.0.0.1\";$IPC_PORT]; h[\`abc\`def\`xyz]" "\`abc\`def\`xyz"
IPC_PORT=19110
test_case "ipc_dict_roundtrip" "listen $IPC_PORT; h: hopen[\"127.0.0.1\";$IPC_PORT]; count h[\`x\`y!10 20]" "2"
IPC_PORT=19111
test_case "ipc_func_roundtrip" "listen $IPC_PORT; h: hopen[\"127.0.0.1\";$IPC_PORT]; (h[{[x] x*2}])@7" "14"

# Range operator tests
test_case "range_basic"          "0..5"      "0 1 2 3 4"
test_case "range_inclusive_basic" "0..=5"    "0 1 2 3 4 5"
test_case "range_nonzero_start"  "3..7"      "3 4 5 6"
test_case "range_negative"       "-2..2"     "-2 -1 0 1"
test_case "range_empty"          "5..5"      "[]"
test_case "range_inclusive_single" "5..=5"   "5"
test_case "range_reverse"        "5..0"      "[]"
test_case "range_type"           "type 0..5" "\`LONG"

# Cast operator ($)
test_case "cast_long_to_int" "\`int\$5" "5i"
test_case "cast_long_to_short" "\`short\$5" "5h"
test_case "cast_long_to_float" "\`float\$5" "5f"
test_case "cast_float_to_int" "\`int\$3.14" "3i"
test_case "cast_float_to_long" "\`long\$3.14" "3"
test_case "cast_float_to_short" "\`short\$3.14" "3h"
test_case "cast_int_to_long" "\`long\$42i" "42"
test_case "cast_short_to_long" "\`long\$42h" "42"
test_case "cast_int_to_bool" "\`bool\$0i" "0b"
test_case "cast_int_to_bool_true" "\`bool\$1i" "1b"
test_case "cast_float_to_bool" "\`bool\$3.14" "1b"
test_case "cast_long_to_char" "\`char\$65" "'A'"
test_case "cast_char_to_int" "\`int\$first\"A\"" "65i"
test_case "cast_float_vector_to_long_atomic" "\`long\$1.5 2.5 3.5" "1 2 3"
test_case "cast_long_vector_to_float_atomic" "\`float\$1 2 3" "1 2 3f"
test_case "cast_long_vector_to_short_atomic" "\`short\$1 2 3" "1 2 3h"
test_case "cast_long_vector_to_int_atomic" "\`int\$1 2 3" "1 2 3i"
test_case "cast_long_vector_to_bool_atomic" "\`bool\$0 1 2" "011b"
test_case "cast_long_vector_to_byte_atomic" "\`byte\$255 0 1" "0xff0001"
test_case "cast_string_to_symbol" "\`symbol\$\"hello\"" "\`hello"
test_case "cast_symbol_to_string" "\`char\$\`hello" "\"hello\""
test_case "cast_list_to_long_atomic" "\`long\$(1;2;3)" "1 2 3"
test_case "cast_empty_type_symbol_errors" "\`\$5" "Error: cast: left argument must be a type symbol or type character"
test_case "cast_non_symbol_left_errors" "5\$3" "Error: cast: left argument must be a type symbol or type character"
test_case "cast_char_h_to_short" "\"h\"\$100" "100h"
test_case "cast_char_i_to_int" "\"i\"\$100" "100i"
test_case "cast_char_j_to_long" "\"j\"\$100" "100"
test_case "cast_char_f_to_float" "\"f\"\$100" "100f"
test_case "cast_char_c_to_char" "\"c\"\$65" "'A'"
test_case "cast_char_b_to_bool" "\"b\"\$42" "1b"
test_case "cast_char_x_to_byte" "\"x\"\$255" "0xff"
test_case "cast_char_unknown_errors" "\"z\"\$5" "Error: cast: left argument must be a type symbol or type character"
test_case "cast_char_longvec_to_shortvec" "\"h\"\$1 2 3" "1 2 3h"
test_case "cast_char_longvec_to_intvec" "\"i\"\$1 2 3" "1 2 3i"
test_case "cast_char_longvec_to_floatvec" "\"f\"\$1 2 3" "1 2 3f"
test_case "cast_char_floatvec_to_longvec" "\"j\"\$1.5 2.5 3.5" "1 2 3"
test_case "cast_char_intvec_to_longvec" "\"j\"\$1 2 3i" "1 2 3"
test_case "cast_char_longvec_to_boolvec" "\"b\"\$0 1 2" "011b"
test_case "cast_char_longvec_to_bytevec" "\"x\"\$255 0 1" "0xff0001"
test_case "cast_char_shortvec_to_longvec" "\"j\"\$1 2 3h" "1 2 3"

# Date casting
test_case "cast_char_d_date"             "\"d\"\$4i"          "1970.01.05"
test_case "cast_sym_date"                "\`date\$4i"         "1970.01.05"
test_case "cast_date_to_long"            "\"j\"\$2026.01.01"  "20454"
test_case "cast_date_to_long_char"       "\"j\"\$2026.01.01"  "20454"
test_case "cast_date_to_int"             "\"i\"\$2026.01.01"  "20454i"
test_case "cast_long_vec_to_date"        "\"d\"\$20454 20455" "2026.01.01 2026.01.02"
test_case "cast_date_char_unknown_err"   "\"z\"\$4i"          "Error: cast: left argument must be a type symbol or type character"
test_case "cast_timestamp_to_date"        "\"d\"\$2026.06.21T12:00:00.000000000"  "2026.06.21"
test_case "cast_null_long_to_float"       "\"f\"\$0N"               "0Nf"
test_case "cast_null_long_to_int"         "\"i\"\$0N"               "0Ni"
test_case "cast_null_long_to_short"       "\"h\"\$0N"               "0Nh"
test_case "cast_null_float_to_long"       "\"j\"\$0Nf"              "0N"
test_case "cast_null_int_to_long"         "\"j\"\$0Ni"              "0N"
test_case "cast_null_long_to_bool"        "\"b\"\$0N"               "0b"
test_case "cast_null_short_roundtrip"     "\"j\"\$(\"h\"\$0N)"     "0N"
test_case "cast_null_float_to_int"        "\"i\"\$0Nf"              "0Ni"
test_case "cast_null_to_same_type"        "\"j\"\$0N"               "0N"
test_case "cast_null_vec_to_float"        "\"f\"\$ 0N 5 0N"         "0N 5 0Nf"
test_case "cast_null_vec_to_int"          "\"i\"\$ 0N 5 0N"         "0N 5 0Ni"
test_case "cast_null_vec_to_short"        "\"h\"\$ 0N 5 0N"         "0N 5 0Nh"

# Operator projection
test_case "op_projection_plus_left" "(+[2;])3" "5"
test_case "op_projection_plus_right" "(+[;3])2" "5"
test_case "op_projection_minus_left" "(-[10;])3" "7"
test_case "op_projection_mul_left" "(*[5;])3" "15"
test_case "op_projection_take" "(#[2;])1 2 3 4 5" "1 2"
test_case "op_projection_drop" "(_[1;])1 2 3 4 5" "2 3 4 5"
test_case "op_projection_max" "(|[10;])3 5 2" "10 10 10"
test_case "op_projection_min" "(&[10;])3 5 2" "3 5 2"
test_case "op_projection_type" "type (+[2;])" "\`projection"
test_case "op_projection_equal_left" "(=[42;])42" "1b"
test_case "op_projection_less_left" "(<[3;])5" "1b"
test_case "op_projection_greater_left" "(>[3;])5" "0b"
test_case "op_projection_power" "(**[2;])3" "8f"
test_case "op_projection_div" "(%[10;])3" "3.33333f"
test_case "op_projection_bare" "+[;]" "+[;]"

# ── compose keyword ─────────────────────────────────────────────────────
test_case "compose_type"             "type compose"            "\`builtin"
test_case "compose_double_sum"       "twicesum:compose({[x] 2*x};+); twicesum[5;2]"  "14"
test_case "compose_single_func"      "f:compose enlist {[x] x*x}; f[5]"    "25"
test_case "compose_neg_abs"          "f:compose(neg;{[x] x}); f[-5]" "5"
test_case "compose_empty_err"        "compose()"               "Error: compose expects a non-empty list"
test_case "compose_type_value"       "type compose(+;{[x] 2*x})"   "\`composition"
test_case "compose_print"            "compose(+;{[x] 2*x})"    "(+;{[x] 2*x})"
test_case "compose_three"            "f:compose(neg;{[x] x+1};{[x] x*2}); f[3]" "-7"
test_case "compose_projection"       "f:compose (*[2];+); f[3;4]" "14"

# Parse error tests (verify no crash, just error message)
test_case "bare_open_paren"               "("       "Error: Failed to parse expression"
test_case "bare_close_paren"              ")"       "Error: Failed to parse expression"
test_case "bare_semicolon"                ";"       ""
test_case "bare_double_semicolon"         ";;"      ""
test_case "bare_trailing_double_semi"      "2+2;;"   ""
test_case "bare_semi_then_expr"           ";2"      "2"
test_case "bare_tilde"                    "~"       "Error: Failed to parse expression"
test_case "bare_backslash"                "\\"      "Error: Failed to parse expression"
test_case "trailing_binary_op"            "1 +"     "Error: Failed to parse expression"
test_case "paren_semicolon_no_expr"       "(;"      "Error: Failed to parse expression"
test_case "paren_semicolon_bad_expr"      "(;~)"    "Error: Failed to parse expression"
test_case "unclosed_bracket_call"         "sum["    "Error: Failed to parse expression"
test_case "bracket_call_bad_expr"         "sum[~]"  "Error: Failed to parse expression"
test_case "func_literal_bad_body"         "{~}"     "Error: Failed to parse expression"
test_case "func_literal_empty_body"       "{;}"     "Error: Failed to parse expression"
test_case "unclosed_string"               '"abc'    "Error: Failed to parse expression"

# ── Table literal tests ──────────────────────────────────────────────
test_case_multiline "table_simple" "([a:1 2 3] b:4 5 6)" $'a b\n- -\n1 4\n2 5\n3 6'
test_case_multiline "table_single_col" "([a:10 20 30])" $'a\n-\n10\n20\n30'
test_case_multiline "table_scalar_cols" "([a:42] b:7)" $'a b\n- -\n42 7'
test_case "table_type" "type ([]a:1 2)" "\`table"
test_case_multiline "table_one_row" "([x:10] y:20)" $'x y\n- -\n10 20'
test_case_multiline "table_expr_cols" "([a:1+1 2+2] b:3+3 4+4)" $'a b\n- -\n4 10\n5 11'
test_case "table_empty" "([])" "()"

# ── Table indexing tests ─────────────────────────────────────────────
test_case "table_index_column_sym" "([]a:1 2 3;b:4 5 6)[\`a]" "1 2 3"
test_case "table_index_column_sym_other" "([]a:1 2 3;b:4 5 6)[\`b]" "4 5 6"
test_case_multiline "table_index_row_zero" "([]a:1 2 3;b:4 5 6) 0" $'`a | 1\n`b | 4'
test_case_multiline "table_index_row_one" "([]a:1 2 3;b:4 5 6) 1" $'`a | 2\n`b | 5'
test_case_multiline "table_index_row_last" "([]a:1 2 3;b:4 5 6) 2" $'`a | 3\n`b | 6'
test_case_multiline "table_index_subtable" "([]a:1 2 3;b:4 5 6) 0 2" $'a b\n- -\n1 4\n3 6'
test_case_multiline "table_index_subtable_reverse" "([]a:1 2 3;b:4 5 6) 2 0" $'a b\n- -\n3 6\n1 4'
test_case_multiline "table_index_subtable_single" "([]a:1 2 3;b:4 5 6) 1 1" $'a b\n- -\n2 5\n2 5'

# ── Flip builtin tests ───────────────────────────────────────────────
test_case "flip_table_to_dict_type" "type flip ([]a:1 2 3;b:4 5 6)" "\`dict"
test_case_multiline "flip_table_to_dict" "flip ([]a:1 2 3;b:4 5 6)" \
  $'`a | 1 2 3\n`b | 4 5 6'
test_case "flip_dict_to_table_type" "type flip \`a\`b!(1 2 3;4 5 6)" "\`table"
test_case_multiline "flip_dict_to_table" "flip \`a\`b!(1 2 3;4 5 6)" \
  $'a b\n- -\n1 4\n2 5\n3 6'
test_case_multiline "flip_flip_table" "flip flip ([]a:1 2 3;b:4 5 6)" \
  $'a b\n- -\n1 4\n2 5\n3 6'
test_case_multiline "flip_dict_scalar_cols" "flip \`a\`b!(42;7)" \
  $'a b\n- -\n42 7'

# ── Enlist dict promotion tests ──────────────────────────────────────
test_case_multiline "enlist_dict_promotion" "(\`a\`b!1 2;\`a\`b!3 4)" \
  $'a b\n- -\n1 2\n3 4'
test_case_multiline "enlist_dict_promotion_single" "enlist[\`a\`b!1 2]" \
  $'a b\n- -\n1 2'
test_case "enlist_dict_promotion_type" "type (\`a\`b!1 2;\`a\`b!3 4)" "\`table"
test_case "enlist_dict_no_promotion" "type (\`a\`b!1 2;\`c\`d!3 4)" "\`list"
test_case_multiline "enlist_dict_promotion_flip" "flip (\`a\`b!1 2;\`a\`b!3 4)" \
  $'`a | 1 3\n`b | 2 4'

# ── Namespace access tests ────────────────────────────────────────────
test_case "ns_assign_basic"     "a.b:5"                  "5"
test_case "ns_read_after_set"   "a.b:5; a.b"             "5"
test_case "ns_multi_key"        "a.b:5; a.c:6; a.b+a.c"  "11"
test_case "ns_overwrite"        "a.b:5; a.b:6; a.b"      "6"
test_case "ns_assign_to_var"    "a.b:5; x: a.b"          "5"
test_case "ns_read_undefined"   "a.b"                    "Error: undefined variable 'a'"
test_case "ns_assign_retains_value" "a.b:5; a.b + 1"     "6"
test_case "ns_nested_assign_read"    "a.b.c:5; a.b.c"      "5"
test_case "ns_nested_multi_field"    "a.b.c:5; a.b.d:6; a.b.c+a.b.d" "11"
test_case "ns_nested_overwrite"      "a.b.c:5; a.b.c:7; a.b.c" "7"
test_case "ns_nested_extend"         "a.x:1; a.b.c:5; a.x" "1"
test_case "ns_nested_deep_read_write" "a.b.c.d:9; a.b.c.d" "9"
test_case "ns_nested_undefined_var"  "a.b.c"               "Error: undefined variable 'a'"
test_case "ns_key_keyword"      "c.d:5; key c"   "\`d"
test_case "ns_value_keyword"    "c.d:5; value c" "5"

# ── Get keyword tests ──────────────────────────────────────────────────
test_case "get_variable"          "a:5; get \`a"                  "5"
test_case "get_undefined"         "get \`x"                       "Error: undefined variable 'x'"
test_case "get_not_symbol"        "get 42"                        "Error: get expects a symbol argument"
test_case "get_namespace_basic"   "a.b:10; get \`a.b"            "10"
test_case "get_nested_namespace"  "a.b.c:7; get \`a.b.c"        "7"
test_case "get_expression"        "a:5; b:a+2; get \`b"         "7"
test_case "get_type"              "a:5; type get \`a"            "\`long"

# ── Where keyword tests ──────────────────────────────────────────────
test_case "where_bool_vec"             "where 0101b"         "1 3"
test_case "where_int_vec"              "where (0 1 0 1 2)"   "1 3 4 4"
test_case "where_bool_scalar_false"    "where 0b"             "[]"
test_case "where_bool_scalar_true"     "where 1b"             "0"
test_case "where_int_zero"              "where 0"              "[]"
test_case "where_int_pos"              "where 3"              "0 0 0"
test_case "where_type_error"           "where 1.5"            "Error: where expects a boolean or integer argument"
test_case "where_overflow_inf"          "where 0W"             "Error: where result too large"
test_case "where_overflow_neginf"       "where -0W"           "Error: where expects non-negative values"

# ── sys.argv tests ────────────────────────────────────────────────────
test_case "sys_argv_count"       "count sys.argv"        "0"
test_case "sys_argv_type"        "type sys.argv"         "\`list"
test_case "sys_type"             "type sys"              "\`dict"
test_case_file_args "sys_argv_with_args"    "count sys.argv"  "4"       "a" "b" "c"
test_case_file_args "sys_argv_index_one"    "sys.argv[1]"     "\"a\""   "a" "b" "c"
test_case_file_args "sys_argv_index_two"    "sys.argv[2]"     "\"b\""   "a" "b" "c"
test_case_file_args "sys_argv_dash_first"   "sys.argv[1]"     "\"-opt\"" "-opt" "val"

# ── sys.hostname tests ─────────────────────────────────────────────────
test_case "sys_hostname_type"    "type sys.hostname"    "\`CHAR"
test_case "sys_hostname_exists"  "0 < count sys.hostname" "1b"

echo ""
echo "Passed: $PASS, Failed: $FAIL"
exit $FAIL
