# API Documentation

_Автоматически сгенерировано api-analyzer_

## Контроллер: ProductController

**Базовый маршрут:** `/products`

### `GET /products/{id}`

- **Функция:** `Product getProduct(...)`
- **Параметры:**
  - `id` (int)
- **Возвращает:** `Product`
- **Проблемы:**
  - Отсутствует валидация входных данных
  - Отсутствует обработка исключений

**Пример запроса:**
```http
GET /products/{id} HTTP/1.1
```

**Пример ответа:**
```json
Product
```

### `DELETE /products/{id}`

- **Функция:** `void deleteProduct(...)`
- **Параметры:**
  - `id` (int)
- **Возвращает:** `void`

**Пример запроса:**
```http
DELETE /products/{id} HTTP/1.1
```

**Пример ответа:**
```json
void
```

### `GET /products/create`

- **Функция:** `Product createProductViaGet(...)`
- **Параметры:**
  - `name` (string)
- **Возвращает:** `Product`
- **Проблемы:**
  - Отсутствует валидация входных данных
  - Отсутствует обработка исключений
  - GET-метод изменяет данные

**Пример запроса:**
```http
GET /products/create HTTP/1.1
```

**Пример ответа:**
```json
Product
```

## Контроллер: UserController

**Базовый маршрут:** `/users`

### `GET /users`

- **Функция:** `List<User> getUsers(...)`
- **Параметры:**
  - `page` (int)
  - `limit` (int)
- **Возвращает:** `List<User>`

**Пример запроса:**
```http
GET /users HTTP/1.1
```

**Пример ответа:**
```json
List<User>
```

### `POST /users`

- **Функция:** `User createUser(...)`
- **Тело запроса:** `UserCreateRequest`
- **Возвращает:** `User`
- **Проблемы:**
  - Отсутствует валидация входных данных
  - Отсутствует обработка исключений

**Пример запроса:**
```http
POST /users HTTP/1.1
```

**Пример ответа:**
```json
User
```

### `GET /users/{id}`

- **Функция:** `User getUserById(...)`
- **Параметры:**
  - `id` (int)
- **Возвращает:** `User`
- **Проблемы:**
  - Отсутствует валидация входных данных
  - Отсутствует обработка исключений

**Пример запроса:**
```http
GET /users/{id} HTTP/1.1
```

**Пример ответа:**
```json
User
```

