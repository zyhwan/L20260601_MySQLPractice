#include <iostream>

#include "jdbc/mysql_connection.h"
#include "jdbc/cppconn/driver.h"
#include "jdbc/cppconn/exception.h"
#include "jdbc/cppconn/resultset.h"
#include "jdbc/cppconn/statement.h"
#include "jdbc/cppconn/prepared_statement.h"


#ifndef _DEBUG
#pragma comment(lib, "mysqlcppconn")
#else
#pragma comment(lib, "debug/mysqlcppconn")
#endif //_DEBUG

int main()
{
	std::string password{};
	std::cin >> password;

	std::string UserID = "zyhwan";
	std::string Password = "a";

	try
	{
		sql::Driver* MyDriver{}; //workbench
		sql::Connection* MyConnection{}; //접속 정보 
		//sql::Statement* MyStatement{}; //쿼리 창
		sql::ResultSet* MyResultSet{}; //결과 창
		sql::PreparedStatement* MyPreparedStatement; //쿼리를 만들때 injection 방어함.

		MyDriver = get_driver_instance();
		MyConnection = MyDriver->connect("tcp://127.0.0.1", "zyhwan", password);

		MyConnection->setSchema("membership");
		//MyStatement = MyConnection->createStatement();
		//sql::SQLString Query = "select * from user where `user_id` = '";
		//Query = Query + UserID + "' and `user_pw` = sha2('";
		//Query = Query + Password + "', 512) and is_delete = 'N';";

		//std::cout << Query << std::endl;

		//MyStatement->executeQuery(Query);

		sql::SQLString Query = "select * from user where `user_id` = ? and `user_pw` = sha2( ?, 512) and is_delete = 'N';";

		MyPreparedStatement = MyConnection->prepareStatement(Query);
		MyPreparedStatement->setString(1, UserID);
		MyPreparedStatement->setString(2, Password);
		MyResultSet = MyPreparedStatement->executeQuery();

		std::cout << Query << std::endl;

		if (MyResultSet->rowsCount() == 0)
		{
			std::cout << "아이디 비번이 틀립니다.";
		}
		else
		{
			//key 설정
			for (; MyResultSet->next();)
			{
				std::cout << MyResultSet->getInt("idx") << std::endl;
				std::cout << MyResultSet->getString("user_id") << std::endl;
				std::cout << MyResultSet->getString("user_pw") << std::endl;
				std::cout << MyResultSet->getString("name") << std::endl;
				std::cout << MyResultSet->getInt("is_deleted") << std::endl;
				std::cout << MyResultSet->getString("created_at") << std::endl;
			}
		}

	}
	catch (sql::SQLException Exception)
	{
		std::cout << Exception.what() << std::endl;
		std::cout << Exception.getSQLState() << std::endl;
	}

	return 0;
}