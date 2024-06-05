#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#define MAXU8 256
#define SIZEINT32 4
#define MURMURHASH_NUMBER 0x5bd1e995
#define DEFAULT_DICT_SIZE 15

#define UPDATE_RATE(dictl) (((dictl) << 1) / 3)
#define MIX_BITS(h) (((((h) ^ ((h) << 10)) ^ ((h) >> 8))) << 5) | ((h) >> (SIZEINT32 * 8 - 4))

/// help functions

int *irange(int from, int to)
{
	int *rn = (int *)malloc(sizeof(int *) * (to - from));
	for (int i = from; i < to; i++)
		rn[i] = i;
	return rn;
}

void rand_shuffle(int *arr, int length)
{
	for (int i = length - 1; i > 0; i--)
	{
		int j = rand() % (i + 1);
		int temp = arr[i];
		arr[i] = arr[j];
		arr[j] = temp;
	}
}

/// general code

typedef struct
{
	char *key;
	char *value;
	bool deleted; // need to saved the order of items in HashDict.values

} DictEntry;

typedef struct
{
	DictEntry **values;
	int length;				 // size of table values
	int taken;				 // taken slots of table values that has a DictEntry and is not deleted
	int filled;				 // filled slots with deleted and taken items
	int premut_table[MAXU8]; //  index table of help hash function

} HashDict;

void HashDict_init(HashDict *self);
void HashDict_clear(HashDict *self);

void HDput(HashDict *self, const char *key, const char *value);
const char *HDget(HashDict *self, const char *key);
void HDremoveEntry(HashDict *self, const char *key);

void HashDict_init(HashDict *self)
{
	self->values = (DictEntry **)malloc(sizeof(DictEntry *) * DEFAULT_DICT_SIZE);

	self->length = DEFAULT_DICT_SIZE;
	self->taken = 0;
	self->filled = 0;

	for (int i = 0; i < DEFAULT_DICT_SIZE; i++)
		self->values[i] = NULL;

	int *numbers = irange(0, MAXU8);

	for (int i = 0; i < MAXU8; i++)
		self->premut_table[i] = numbers[i];

	free(numbers);

	rand_shuffle(self->premut_table, MAXU8);
}

void HashDict_resize(HashDict *self)
{
	// saving of old values
	DictEntry **old_values = (DictEntry **)malloc(sizeof(DictEntry *) * self->taken);

	if (old_values == NULL)
	{
		fprintf(stderr, "Failed to allocate memory for old values during resize!");
		HashDict_clear(self);
		exit(EXIT_FAILURE);
	}

	int old_values_number = self->taken,
		saved_index = 0;

	for (int i = 0; i < self->length; i++)
	{
		if (saved_index >= old_values_number)
			break;

		if (self->values[i] != NULL)
		{
			if (!self->values[i]->deleted)
				old_values[saved_index++] = self->values[i];

			self->values[i] = NULL;
		}
	}

	// length x 2
	self->values = (DictEntry **)realloc(self->values, sizeof(DictEntry *) * self->length);
	self->length *= 2;
	self->taken = 0;
	self->filled = 0;

	// rehash all of saved values
	for (int i = 0; i < old_values_number; i++)
	{
		DictEntry *entry = old_values[i];
		HDput(self, entry->key, entry->value);
	}
	free(old_values);
}

unsigned int HashDict_MurmurHash2(const char *key, unsigned int seed)
{
	const int right_shift = 24; // number of bit shifts to the right

	unsigned int len = strlen(key),
				 hash = seed ^ len, // Initialize hash key length
		chunk = 0;

	// Iterating through the data 4 bytes (32 bits) per iteration
	while (len >= 4)
	{
		// Forming a 32-bit data block
		chunk = key[0] | (key[1] << 8) | (key[2] << 16) | (key[3] << 24);

		chunk *= MURMURHASH_NUMBER;
		chunk ^= chunk >> right_shift;
		chunk *= MURMURHASH_NUMBER;

		hash *= MURMURHASH_NUMBER;
		hash ^= chunk;

		key += 4;
		len -= 4;
	}

	// Processing the remaining bytes (if the length is not a multiple of 4)
	switch (len)
	{
	case 3:
		hash ^= key[2] << 16;
	case 2:
		hash ^= key[1] << 8;
	case 1:
		hash ^= key[0];
		hash *= MURMURHASH_NUMBER;
	};

	hash ^= hash >> 13;
	hash *= MURMURHASH_NUMBER;
	hash ^= hash >> 15;

	return hash;
}

unsigned int HashDict_hashMultShift(const char *key, unsigned int seed)
{
	unsigned int hash_number = seed,
				 prime = 2654435769u;
	while (*key)
		hash_number = (hash_number ^ *key++) * prime;
	return hash_number;
}

unsigned int HashDict_simpleHash(HashDict *self, const char *key)
{
	unsigned int hash_number = 0;
	unsigned int second_hash = HashDict_hashMultShift(key, MIX_BITS(self->premut_table[strlen(key) % MAXU8]));

	while (*key)
		hash_number = (MIX_BITS(hash_number) << 5) - second_hash + *key++;
	return hash_number; // result hash
}

void HDput(HashDict *self, const char *key, const char *value)
{
	if (self->filled >= UPDATE_RATE(self->length))
		HashDict_resize(self); // Resize if dict filled by 2/3

	unsigned int index = HashDict_MurmurHash2(key, 0) % self->length;
	unsigned int second_hash = HashDict_simpleHash(self, key);
	int offset = 1;

	// <DEBUG>
	//printf("Key: %s\n", key);
	// </DEBUG>

	while (self->values[index] != NULL && strcmp(self->values[index]->key, key) != 0)
	{
		// DEBUG
		//printf("try: %d of '%s' (%s) [%d]\n", offset, key, offset > 3 ? "bad" : "good", index);
		// </DEBUG>

		// Apply double hashing
		index = (index + offset++ * second_hash) % self->length;

		if (offset >= 2)
			index = (index + HashDict_MurmurHash2(key, offset)) % self->length;
	}

	if (self->values[index] == NULL)
	{
		// Create new entry
		DictEntry *new_entry = (DictEntry *)malloc(sizeof(DictEntry));
		new_entry->key = strdup(key);
		new_entry->value = strdup(value);
		new_entry->deleted = false;

		self->values[index] = new_entry;
		self->taken++;
		self->filled++;
	}
	else if (self->values[index]->deleted)
	{
		// Rewrite deleted
		free(self->values[index]->key);
		free(self->values[index]->value);

		self->values[index]->key = strdup(key);
		self->values[index]->value = strdup(value);
		self->values[index]->deleted = false;
		self->taken--;
	}
	else
	{
		// Update entry value
		free(self->values[index]->value);
		self->values[index]->value = strdup(value);
	}
}

const char *HDget(HashDict *self, const char *key)
{
	unsigned int index = HashDict_MurmurHash2(key, 0) % self->length;
	unsigned int second_hash = HashDict_simpleHash(self, key);
	int offset = 1;
	bool found = false;

	while (1)
	{
		if (index >= 0 && index < self->length)
		{
			
			if (self->values[index] == NULL)
				break;
				
			if (!self->values[index]->deleted && !strcmp(self->values[index]->key, key))
			{
				found = true;
				break;
			}
		}
		
		index = (index + offset++ * second_hash) % self->length;

		if (offset >= 2)
			index = (index + HashDict_MurmurHash2(key, offset)) % self->length;
		
		if (index >= self->length) 
        	index %= self->length;
	}
	
	if (found)
		return self->values[index]->value;

	return NULL;
}

void HDremoveEntry(HashDict *self, const char *key)
{
	unsigned int index = HashDict_MurmurHash2(key, 0) % self->length;
	unsigned int second_hash = HashDict_simpleHash(self, key);
	int offset = 1;

	while (self->values[index] != NULL)
	{
		if (!self->values[index]->deleted && !strcmp(self->values[index]->key, key))
		{
			self->values[index]->deleted = true;
			break;
		}

		index = (index + offset++ * second_hash) % self->length;

		if (offset >= 2)
			index = (index + HashDict_MurmurHash2(key, offset)) % self->length;
	}
}

void HashDict_clear(HashDict *self)
{
	for (int i = 0; i < self->length; i++)
	{
		DictEntry *entry = self->values[i];
		if (entry != NULL)
		{
			free(entry->key);
			free(entry->value);
			free(entry);
		}
	}
	free(self->values);
}

void printHD(HashDict *hd)
{
	printf("{");
	for (int i = 0; i < hd->length; i++)
	{
		if (hd->values[i] != NULL)
		{
			printf("\t'%s': '%s'(%d),\n", hd->values[i]->key, hd->values[i]->value, i);
		}
	}
	printf("}\n");
}

void HDspacePrint(HashDict *hd)
{
	printf("###########################\n");
	for (int i = 0; i < hd->length; i++)
	{
		printf("======\n[ %d ]", i);
		if (hd->values[i] != NULL)
		{
			printf("(%s) => (%s)\n", hd->values[i]->key, hd->values[i]->value);
		}
		else
			printf("...\n");
	}
}

const char testlist[115][2][100] = {
	{"car", "машина"},
	{"red", "красный"},
	{"go", "идти"},
	{"tree", "дерево"},
	{"water", "вода"},
	{"sun", "солнце"},
	{"house", "дом"},
	{"cat", "кошка"},
	{"dog", "собака"},
	{"book", "книга"},
	{"apple", "яблоко"},
	{"computer", "компьютер"},
	{"sky", "небо"},
	{"rain", "дождь"},
	{"moon", "луна"},
	{"flower", "цветок"},
	{"bird", "птица"},
	{"friend", "друг"},
	{"time", "время"},
	{"love", "любовь"},
	{"money", "деньги"},
	{"music", "музыка"},
	{"city", "город"},
	{"world", "мир"},
	{"smile", "улыбка"},
	{"language", "язык"},
	{"earth", "земля"},
	{"fire", "огонь"},
	{"family", "семья"},
	{"food", "еда"},
	{"morning", "утро"},
	{"night", "ночь"},
	{"shirt", "рубашка"},
	{"shoe", "туфля"},
	{"star", "звезда"},
	{"wind", "ветер"},
	{"problem", "проблема"},
	{"name", "имя"},
	{"color", "цвет"},
	{"friendship", "дружба"},
	{"picture", "картина"},
	{"phone", "телефон"},
	{"chair", "стул"},
	{"table", "стол"},
	{"street", "улица"},
	{"carpet", "ковер"},
	{"window", "окно"},
	{"door", "дверь"},
	{"pencil", "карандаш"},
	{"pen", "ручка"},
	{"paper", "бумага"},
	{"watermelon", "арбуз"},
	{"banana", "банан"},
	{"orange", "апельсин"},
	{"grape", "виноград"},
	{"lemon", "лимон"},
	{"peach", "персик"},
	{"cherry", "вишня"},
	{"strawberry", "клубника"},
	{"melon", "дыня"},
	{"pineapple", "ананас"},
	{"kiwi", "киви"},
	{"pear", "груша"},
	{"apricot", "абрикос"},
	{"plum", "слива"},
	{"cucumber", "огурец"},
	{"tomato", "помидор"},
	{"potato", "картофель"},
	{"carrot", "морковь"},
	{"onion", "лук"},
	{"garlic", "чеснок"},
	{"pepper", "перец"},
	{"cabbage", "капуста"},
	{"lettuce", "салат"},
	{"broccoli", "брокколи"},
	{"cauliflower", "цветная капуста"},
	{"bean", "бобы"},
	{"corn", "кукуруза"},
	{"pea", "горошек"},
	{"rice", "рис"},
	{"bread", "хлеб"},
	{"butter", "масло"},
	{"cheese", "сыр"},
	{"milk", "молоко"},
	{"egg", "яйцо"},
	{"meat", "мясо"},
	{"chicken", "курица"},
	{"fish", "рыба"},
	{"pork", "свинина"},
	{"beef", "говядина"},
	{"lamb", "баранина"},
	{"sausage", "сосиска"},
	{"bacon", "бекон"},
	{"ham", "ветчина"}
};

#include <stdio.h>
#include <sys/time.h>

#define TESTLEN 93

int input(char *out, char **in)
{
	size_t len = 0;
	int read;
	printf("%s", out);
	read = getline(in, &len, stdin);
	if (read == -1)
		return -1;
	(*in)[read - 1] = '\0';
	return read;
}

void read_variables()
{
	HashDict engdict;
	HashDict_init(&engdict);

	struct timeval start_time, end_time;
	double elapsed_time;

	char *line = NULL,
		 key[50] = {0}, value[50] = {0};
	int number_word = 0;

	gettimeofday(&start_time, NULL);
	for (int i = 0; i < TESTLEN; i++)
	{
		HDput(&engdict, testlist[i][0], testlist[i][1]);
	}
	gettimeofday(&end_time, NULL);
	elapsed_time = (end_time.tv_sec - start_time.tv_sec) +
				   (end_time.tv_usec - start_time.tv_usec) / 1000000.0;

	printf("Dict English\n");
	printf("load time: %f secs\n", elapsed_time);

	while (1)
	{
		if (input("\n> ", &line) == -1)
		{
			fprintf(stderr, "error while reading a line!");
			break;
		}
		number_word = sscanf(line, "%s %s", key, value);
		
		if (number_word == 2)
		{
			printf("************\n");
			gettimeofday(&start_time, NULL);
			HDput(&engdict, key, value);
			gettimeofday(&end_time, NULL);
			elapsed_time = (end_time.tv_sec - start_time.tv_sec) +
						   (end_time.tv_usec - start_time.tv_usec) / 1000000.0;
			printf("time: %f secs\n************\n", elapsed_time);
		}

		else
		{
			printf("************\n");
			gettimeofday(&start_time, NULL);
			const char *result = HDget(&engdict, key);
			gettimeofday(&end_time, NULL);
			elapsed_time = (end_time.tv_sec - start_time.tv_sec) +
						   (end_time.tv_usec - start_time.tv_usec) / 1000000.0;

			if (result != NULL)
			{
				strcpy(value, result);
				printf("%s\n", value);
			}
			
			else
				printf("Key not found '%s'\n", key);

			printf("time: %f secs\n************\n", elapsed_time);
		}
		
	}

	free(line);
	HashDict_clear(&engdict);
}

int main()
{
	read_variables();
	return 0;
}
