import math

table_size = 512
table_row_size = 16

num_rows = int(table_size / table_row_size)

for row in range(num_rows):
	print()
	for col in range(table_row_size):
		if (row == 0 and col == 0):
			print("0", end="")
		else:
			print(math.floor(math.log2(row * table_row_size + col) + 1), end="")
		if (col != (table_row_size - 1)):
			print(", ", end="")
		elif ((row != (num_rows - 1)) or (col != (table_row_size - 1))):
			print(",", end="")
print()