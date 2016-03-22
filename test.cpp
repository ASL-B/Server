#include"server.cpp"
#include"serialize.cpp"

int main()
{
	string a = "hello";
	string b = "123";
	getSerializeStream(a, b);
	cout << b << endl;
	//Server ser;
	//ser.inti();
	//ser.process();
	return 0;
}
