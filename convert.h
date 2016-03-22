#include <sstream>
using namespace std;

string itos(int i)
{
	stringstream ss;
	ss << i;
	string res;
	ss >> res;
	return res;
}

int stoi(char*s)
{
	stringstream ss;
	ss << s;
	int res;
	ss >> res;
	return res;
}

string btos(bool i)
{
	stringstream ss;
	ss << i;
	string res;
	ss >> res;
	return res;
}

bool stob(string s)
{
	stringstream ss;
	ss << s;
	bool res;
	ss >> res;
	return res;
}
