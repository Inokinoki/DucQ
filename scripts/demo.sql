-- Demo state used by CI screenshots and manual smoke tests.
CREATE TABLE users AS
SELECT i AS id,
       'user_' || i AS name,
       (i * 37 % 90 + 18) AS age,
       (i % 5)::VARCHAR AS tier
FROM range(1, 101) t(i);
SELECT id, name, age, tier
FROM users
ORDER BY id
LIMIT 12;
