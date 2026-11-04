#!/usr/bin/env python3
"""Fill DucQ .ts translation files with the translations below and report gaps.

Usage: python3 scripts/fill_translations.py
"""
import glob
import sys
import xml.etree.ElementTree as ET

# source -> {lang: translation}; entries missing a lang fall back to the source
# string (kept untranslated) and are reported by this script.
T = {
    "Failed to create the database configuration": {
        "zh_CN": "无法创建数据库配置",
        "ja": "データベース設定の作成に失敗しました",
        "ko": "데이터베이스 구성을 만들지 못했습니다",
        "de": "Datenbankkonfiguration konnte nicht erstellt werden",
        "fr": "Échec de la création de la configuration de la base de données",
        "es": "No se pudo crear la configuración de la base de datos",
        "pt_BR": "Falha ao criar a configuração do banco de dados",
        "ru": "Не удалось создать конфигурацию базы данных",
    },
    "Failed to open the database": {
        "zh_CN": "无法打开数据库",
        "ja": "データベースを開けませんでした",
        "ko": "데이터베이스를 열지 못했습니다",
        "de": "Datenbank konnte nicht geöffnet werden",
        "fr": "Échec de l'ouverture de la base de données",
        "es": "No se pudo abrir la base de datos",
        "pt_BR": "Falha ao abrir o banco de dados",
        "ru": "Не удалось открыть базу данных",
    },
    "&File": {
        "zh_CN": "文件(&F)", "ja": "ファイル(&F)", "ko": "파일(&F)", "de": "&Datei",
        "fr": "&Fichier", "es": "&Archivo", "pt_BR": "&Arquivo", "ru": "&Файл",
    },
    "&Language": {
        "zh_CN": "语言(&L)", "ja": "言語(&L)", "ko": "언어(&L)", "de": "&Sprache",
        "fr": "&Langue", "es": "&Idioma", "pt_BR": "&Idioma", "ru": "&Язык",
    },
    "A query is running. Wait for it to finish or cancel it first.": {
        "zh_CN": "查询进行中，请等待完成或点击取消后再操作。",
        "ja": "クエリを実行中です。完了を待つか、先にキャンセルしてください。",
        "ko": "쿼리가 실행 중입니다. 완료를 기다리거나 먼저 취소해 주세요.",
        "de": "Eine Abfrage läuft. Bitte warten Sie auf den Abschluss oder brechen Sie zuerst ab.",
        "fr": "Une requête est en cours. Attendez qu'elle se termine ou annulez-la d'abord.",
        "es": "Hay una consulta en curso. Espere a que termine o cancélela primero.",
        "pt_BR": "Há uma consulta em andamento. Aguarde a conclusão ou cancele-a primeiro.",
        "ru": "Выполняется запрос. Дождитесь завершения или отмените его.",
    },
    "Cancel": {
        "zh_CN": "取消", "ja": "キャンセル", "ko": "취소", "de": "Abbrechen",
        "fr": "Annuler", "es": "Cancelar", "pt_BR": "Cancelar", "ru": "Отмена",
    },
    "Close Database (back to in-memory)": {
        "zh_CN": "关闭数据库（回到内存库）",
        "ja": "データベースを閉じる（メモリ内データベースに戻る）",
        "ko": "데이터베이스 닫기(인메모리 데이터베이스로 돌아가기)",
        "de": "Datenbank schließen (zurück zur In-Memory-Datenbank)",
        "fr": "Fermer la base de données (retour à la base en mémoire)",
        "es": "Cerrar la base de datos (volver a la base en memoria)",
        "pt_BR": "Fechar o banco de dados (voltar ao banco em memória)",
        "ru": "Закрыть базу данных (вернуться к базе в памяти)",
    },
    "Done: %L1 rows · %L2 ms": {
        "zh_CN": "完成：%L1 行 · %L2 ms",
        "ja": "完了：%L1 行 · %L2 ms",
        "ko": "완료: %L1행 · %L2 ms",
        "de": "Fertig: %L1 Zeilen · %L2 ms",
        "fr": "Terminé : %L1 lignes · %L2 ms",
        "es": "Hecho: %L1 filas · %L2 ms",
        "pt_BR": "Concluído: %L1 linhas · %L2 ms",
        "ru": "Готово: %L1 строк · %L2 ms",
    },
    "DuckDB databases (*.duckdb *.db *.ddb);;All files (*)": {
        "zh_CN": "DuckDB 数据库 (*.duckdb *.db *.ddb);;所有文件 (*)",
        "ja": "DuckDB データベース (*.duckdb *.db *.ddb);;すべてのファイル (*)",
        "ko": "DuckDB 데이터베이스 (*.duckdb *.db *.ddb);;모든 파일 (*)",
        "de": "DuckDB-Datenbanken (*.duckdb *.db *.ddb);;Alle Dateien (*)",
        "fr": "Bases de données DuckDB (*.duckdb *.db *.ddb);;Tous les fichiers (*)",
        "es": "Bases de datos DuckDB (*.duckdb *.db *.ddb);;Todos los archivos (*)",
        "pt_BR": "Bancos de dados DuckDB (*.duckdb *.db *.ddb);;Todos os arquivos (*)",
        "ru": "Базы данных DuckDB (*.duckdb *.db *.ddb);;Все файлы (*)",
    },
    "DuckDB databases (*.duckdb *.db);;All files (*)": {
        "zh_CN": "DuckDB 数据库 (*.duckdb *.db);;所有文件 (*)",
        "ja": "DuckDB データベース (*.duckdb *.db);;すべてのファイル (*)",
        "ko": "DuckDB 데이터베이스 (*.duckdb *.db);;모든 파일 (*)",
        "de": "DuckDB-Datenbanken (*.duckdb *.db);;Alle Dateien (*)",
        "fr": "Bases de données DuckDB (*.duckdb *.db);;Tous les fichiers (*)",
        "es": "Bases de datos DuckDB (*.duckdb *.db);;Todos los archivos (*)",
        "pt_BR": "Bancos de dados DuckDB (*.duckdb *.db);;Todos os arquivos (*)",
        "ru": "Базы данных DuckDB (*.duckdb *.db);;Все файлы (*)",
    },
    "Exit": {
        "zh_CN": "退出", "ja": "終了", "ko": "종료", "de": "Beenden",
        "fr": "Quitter", "es": "Salir", "pt_BR": "Sair", "ru": "Выход",
    },
    "Export": {
        "zh_CN": "导出", "ja": "エクスポート", "ko": "내보내기", "de": "Exportieren",
        "fr": "Exporter", "es": "Exportar", "pt_BR": "Exportar", "ru": "Экспорт",
    },
    "Export Current Results as CSV…": {
        "zh_CN": "将当前结果导出为 CSV…",
        "ja": "現在の結果を CSV としてエクスポート…",
        "ko": "현재 결과를 CSV로 내보내기…",
        "de": "Aktuelle Ergebnisse als CSV exportieren…",
        "fr": "Exporter les résultats actuels en CSV…",
        "es": "Exportar los resultados actuales como CSV…",
        "pt_BR": "Exportar os resultados atuais como CSV…",
        "ru": "Экспортировать текущие результаты в CSV…",
    },
    "Export Current Results as Parquet…": {
        "zh_CN": "将当前结果导出为 Parquet…",
        "ja": "現在の結果を Parquet としてエクスポート…",
        "ko": "현재 결과를 Parquet로 내보내기…",
        "de": "Aktuelle Ergebnisse als Parquet exportieren…",
        "fr": "Exporter les résultats actuels en Parquet…",
        "es": "Exportar los resultados actuales como Parquet…",
        "pt_BR": "Exportar os resultados atuais como Parquet…",
        "ru": "Экспортировать текущие результаты в Parquet…",
    },
    "Export Results": {
        "zh_CN": "导出结果", "ja": "結果のエクスポート", "ko": "결과 내보내기",
        "de": "Ergebnisse exportieren", "fr": "Exporter les résultats",
        "es": "Exportar resultados", "pt_BR": "Exportar resultados", "ru": "Экспорт результатов",
    },
    "Failed to initialize the in-memory database: %1": {
        "zh_CN": "初始化内存数据库失败：%1",
        "ja": "インメモリデータベースの初期化に失敗しました: %1",
        "ko": "인메모리 데이터베이스 초기화 실패: %1",
        "de": "In-Memory-Datenbank konnte nicht initialisiert werden: %1",
        "fr": "Échec de l'initialisation de la base en mémoire : %1",
        "es": "No se pudo inicializar la base de datos en memoria: %1",
        "pt_BR": "Falha ao inicializar o banco de dados em memória: %1",
        "ru": "Не удалось инициализировать базу данных в памяти: %1",
    },
    "In-memory database": {
        "zh_CN": "内存数据库",
        "ja": "インメモリデータベース",
        "ko": "인메모리 데이터베이스",
        "de": "In-Memory-Datenbank",
        "fr": "Base de données en mémoire",
        "es": "Base de datos en memoria",
        "pt_BR": "Banco de dados em memória",
        "ru": "База данных в памяти",
    },
    "New Database…": {
        "zh_CN": "新建数据库…", "ja": "新規データベース…", "ko": "새 데이터베이스…",
        "de": "Neue Datenbank…", "fr": "Nouvelle base de données…",
        "es": "Nueva base de datos…", "pt_BR": "Novo banco de dados…", "ru": "Новая база данных…",
    },
    "New DuckDB Database": {
        "zh_CN": "新建 DuckDB 数据库",
        "ja": "新規 DuckDB データベース",
        "ko": "새 DuckDB 데이터베이스",
        "de": "Neue DuckDB-Datenbank",
        "fr": "Nouvelle base de données DuckDB",
        "es": "Nueva base de datos DuckDB",
        "pt_BR": "Novo banco de dados DuckDB",
        "ru": "Новая база данных DuckDB",
    },
    "No SQL to execute": {
        "zh_CN": "没有可执行的 SQL",
        "ja": "実行する SQL がありません",
        "ko": "실행할 SQL이 없습니다",
        "de": "Kein SQL zum Ausführen",
        "fr": "Aucun SQL à exécuter",
        "es": "No hay SQL que ejecutar",
        "pt_BR": "Nenhum SQL para executar",
        "ru": "Нет SQL для выполнения",
    },
    "Not connected": {
        "zh_CN": "未连接", "ja": "未接続", "ko": "연결되지 않음", "de": "Nicht verbunden",
        "fr": "Non connecté", "es": "Sin conexión", "pt_BR": "Não conectado", "ru": "Нет подключения",
    },
    "Open Database…": {
        "zh_CN": "打开数据库…", "ja": "データベースを開く…", "ko": "데이터베이스 열기…",
        "de": "Datenbank öffnen…", "fr": "Ouvrir une base de données…",
        "es": "Abrir base de datos…", "pt_BR": "Abrir banco de dados…", "ru": "Открыть базу данных…",
    },
    "Open DuckDB Database": {
        "zh_CN": "打开 DuckDB 数据库",
        "ja": "DuckDB データベースを開く",
        "ko": "DuckDB 데이터베이스 열기",
        "de": "DuckDB-Datenbank öffnen",
        "fr": "Ouvrir une base de données DuckDB",
        "es": "Abrir base de datos DuckDB",
        "pt_BR": "Abrir banco de dados DuckDB",
        "ru": "Открыть базу данных DuckDB",
    },
    "Open Failed": {
        "zh_CN": "打开失败", "ja": "オープン失敗", "ko": "열기 실패",
        "de": "Öffnen fehlgeschlagen", "fr": "Échec de l'ouverture",
        "es": "Error al abrir", "pt_BR": "Falha ao abrir", "ru": "Ошибка открытия",
    },
    "Query cancelled": {
        "zh_CN": "查询已取消",
        "ja": "クエリがキャンセルされました",
        "ko": "쿼리가 취소되었습니다",
        "de": "Abfrage abgebrochen",
        "fr": "Requête annulée",
        "es": "Consulta cancelada",
        "pt_BR": "Consulta cancelada",
        "ru": "Запрос отменён",
    },
    "Query history": {
        "zh_CN": "查询历史", "ja": "クエリ履歴", "ko": "쿼리 기록", "de": "Abfrageverlauf",
        "fr": "Historique des requêtes", "es": "Historial de consultas",
        "pt_BR": "Histórico de consultas", "ru": "История запросов",
    },
    "Refresh Schema": {
        "zh_CN": "刷新模式", "ja": "スキーマを更新", "ko": "스키마 새로 고침",
        "de": "Schema aktualisieren", "fr": "Actualiser le schéma",
        "es": "Actualizar esquema", "pt_BR": "Atualizar esquema", "ru": "Обновить схему",
    },
    "Run (Ctrl+⏎)": {
        "zh_CN": "执行 (Ctrl+⏎)", "ja": "実行 (Ctrl+⏎)", "ko": "실행 (Ctrl+⏎)",
        "de": "Ausführen (Ctrl+⏎)", "fr": "Exécuter (Ctrl+⏎)", "es": "Ejecutar (Ctrl+⏎)",
        "pt_BR": "Executar (Ctrl+⏎)", "ru": "Запустить (Ctrl+⏎)",
    },
    "Running…": {
        "zh_CN": "执行中…", "ja": "実行中…", "ko": "실행 중…", "de": "Läuft…",
        "fr": "En cours…", "es": "Ejecutando…", "pt_BR": "Em execução…", "ru": "Выполняется…",
    },
    "Schema refreshed: %1 tables/views": {
        "zh_CN": "模式已刷新：%1 个表/视图",
        "ja": "スキーマを更新しました: %1 個のテーブル/ビュー",
        "ko": "스키마 새로 고침 완료: 테이블/뷰 %1개",
        "de": "Schema aktualisiert: %1 Tabellen/Views",
        "fr": "Schéma actualisé : %1 tables/vues",
        "es": "Esquema actualizado: %1 tablas/vistas",
        "pt_BR": "Esquema atualizado: %1 tabelas/views",
        "ru": "Схема обновлена: таблиц/представлений: %1",
    },
    "Select a finished result tab first.": {
        "zh_CN": "请先选中一个已完成查询的结果页签。",
        "ja": "まず完了した結果タブを選択してください。",
        "ko": "먼저 완료된 결과 탭을 선택해 주세요.",
        "de": "Bitte wählen Sie zuerst einen abgeschlossenen Ergebnis-Tab.",
        "fr": "Sélectionnez d'abord un onglet de résultats terminé.",
        "es": "Seleccione primero una pestaña de resultados finalizada.",
        "pt_BR": "Selecione primeiro uma aba de resultados concluída.",
        "ru": "Сначала выберите завершённую вкладку с результатами.",
    },
    "Skipped unsupported file: %1": {
        "zh_CN": "跳过不支持的文件：%1",
        "ja": "サポートされていないファイルをスキップしました: %1",
        "ko": "지원되지 않는 파일 건너뜀: %1",
        "de": "Nicht unterstützte Datei übersprungen: %1",
        "fr": "Fichier non pris en charge ignoré : %1",
        "es": "Se omitió el archivo no compatible: %1",
        "pt_BR": "Arquivo não suportado ignorado: %1",
        "ru": "Пропущен неподдерживаемый файл: %1",
    },
    "Database connection not initialized": {
        "zh_CN": "数据库连接未初始化",
        "ja": "データベース接続が初期化されていません",
        "ko": "데이터베이스 연결이 초기화되지 않았습니다",
        "de": "Datenbankverbindung nicht initialisiert",
        "fr": "Connexion à la base de données non initialisée",
        "es": "Conexión a la base de datos no inicializada",
        "pt_BR": "Conexão com o banco de dados não inicializada",
        "ru": "Подключение к базе данных не инициализировано",
    },
    "Execution failed": {
        "zh_CN": "执行出错", "ja": "実行に失敗しました", "ko": "실행 실패",
        "de": "Ausführung fehlgeschlagen", "fr": "Échec de l'exécution",
        "es": "Error de ejecución", "pt_BR": "Falha na execução", "ru": "Ошибка выполнения",
    },
    "Failed to parse the SQL": {
        "zh_CN": "无法解析 SQL",
        "ja": "SQL を解析できませんでした",
        "ko": "SQL을 구문 분석하지 못했습니다",
        "de": "SQL konnte nicht analysiert werden",
        "fr": "Échec de l'analyse du SQL",
        "es": "No se pudo analizar el SQL",
        "pt_BR": "Falha ao analisar o SQL",
        "ru": "Не удалось разобрать SQL",
    },
    "Failed to prepare statement": {
        "zh_CN": "预处理语句失败",
        "ja": "ステートメントの準備に失敗しました",
        "ko": "구문 준비 실패",
        "de": "Anweisung konnte nicht vorbereitet werden",
        "fr": "Échec de la préparation de l'instruction",
        "es": "No se pudo preparar la instrucción",
        "pt_BR": "Falha ao preparar a instrução",
        "ru": "Не удалось подготовить инструкцию",
    },
    "Statement %1/%2: %3": {
        "zh_CN": "语句 %1/%2: %3", "ja": "ステートメント %1/%2: %3", "ko": "구문 %1/%2: %3",
        "de": "Anweisung %1/%2: %3", "fr": "Instruction %1/%2 : %3",
        "es": "Instrucción %1/%2: %3", "pt_BR": "Instrução %1/%2: %3", "ru": "Инструкция %1/%2: %3",
    },
    "prepare failed": {
        "zh_CN": "预处理失败", "ja": "準備に失敗", "ko": "준비 실패",
        "de": "Vorbereitung fehlgeschlagen", "fr": "Échec de la préparation",
        "es": "Error al preparar", "pt_BR": "Falha ao preparar", "ru": "ошибка подготовки",
    },
    " (of %L1 rows, showing first %L2)": {
        "zh_CN": "（共 %L1 行，仅展示前 %L2 行）",
        "ja": "（全 %L1 行中、先頭 %L2 行を表示）",
        "ko": "(총 %L1행 중 처음 %L2행 표시)",
        "de": " (von %L1 Zeilen, erste %L2 angezeigt)",
        "fr": " (sur %L1 lignes, %L2 premières affichées)",
        "es": " (de %L1 filas, mostrando las primeras %L2)",
        "pt_BR": " (de %L1 linhas, exibindo as primeiras %L2)",
        "ru": " (из %L1 строк, показаны первые %L2)",
    },
    "%L1 rows · %L2 ms": {
        "zh_CN": "%L1 行 · %L2 ms", "ja": "%L1 行 · %L2 ms", "ko": "%L1행 · %L2 ms",
        "de": "%L1 Zeilen · %L2 ms", "fr": "%L1 lignes · %L2 ms", "es": "%L1 filas · %L2 ms",
        "pt_BR": "%L1 linhas · %L2 ms", "ru": "%L1 строк · %L2 ms",
    },
    "Cancelled · %L1 ms": {
        "zh_CN": "已取消 · %L1 ms", "ja": "キャンセル済み · %L1 ms", "ko": "취소됨 · %L1 ms",
        "de": "Abgebrochen · %L1 ms", "fr": "Annulé · %L1 ms", "es": "Cancelado · %L1 ms",
        "pt_BR": "Cancelado · %L1 ms", "ru": "Отменено · %L1 ms",
    },
    "Result %1": {
        "zh_CN": "结果 %1", "ja": "結果 %1", "ko": "결과 %1", "de": "Ergebnis %1",
        "fr": "Résultat %1", "es": "Resultado %1", "pt_BR": "Resultado %1", "ru": "Результат %1",
    },
    "Object": {
        "zh_CN": "对象", "ja": "オブジェクト", "ko": "개체", "de": "Objekt",
        "fr": "Objet", "es": "Objeto", "pt_BR": "Objeto", "ru": "Объект",
    },
    "Type": {
        "zh_CN": "类型", "ja": "型", "ko": "유형", "de": "Typ",
        "fr": "Type", "es": "Tipo", "pt_BR": "Tipo", "ru": "Тип",
    },
    "-- Ctrl+Enter runs the SQL, e.g.:\nSELECT * FROM range(10);": {
        "zh_CN": "-- Ctrl+Enter 执行 SQL，示例：\nSELECT * FROM range(10);",
        "ja": "-- Ctrl+Enter で SQL を実行、例：\nSELECT * FROM range(10);",
        "ko": "-- Ctrl+Enter로 SQL 실행, 예:\nSELECT * FROM range(10);",
        "de": "-- Ctrl+Enter führt das SQL aus, z. B.:\nSELECT * FROM range(10);",
        "fr": "-- Ctrl+Entrée exécute le SQL, ex. :\nSELECT * FROM range(10);",
        "es": "-- Ctrl+Intro ejecuta el SQL, p. ej.:\nSELECT * FROM range(10);",
        "pt_BR": "-- Ctrl+Enter executa o SQL, ex.:\nSELECT * FROM range(10);",
        "ru": "-- Ctrl+Enter запускает SQL, напр.:\nSELECT * FROM range(10);",
    },
    # Identical in every language; recorded so the script marks them finished.
    "CSV (*.csv)": {},
    "Parquet (*.parquet)": {},
    "Active connection": {
        "zh_CN": "当前连接", "ja": "アクティブな接続", "ko": "활성 연결",
        "de": "Aktive Verbindung", "fr": "Connexion active", "es": "Conexión activa",
        "pt_BR": "Conexão ativa", "ru": "Активное подключение",
    },
    "Already open in another connection: %1": {
        "zh_CN": "已在其他连接中打开：%1",
        "ja": "別の接続で既に開かれています: %1",
        "ko": "다른 연결에서 이미 열려 있음: %1",
        "de": "Bereits in einer anderen Verbindung geöffnet: %1",
        "fr": "Déjà ouvert dans une autre connexion : %1",
        "es": "Ya abierto en otra conexión: %1",
        "pt_BR": "Já aberto em outra conexão: %1",
        "ru": "Уже открыто в другом подключении: %1",
    },
    "Cannot read script: %1": {
        "zh_CN": "无法读取脚本：%1",
        "ja": "スクリプトを読み取れません: %1",
        "ko": "스크립트를 읽을 수 없습니다: %1",
        "de": "Skript kann nicht gelesen werden: %1",
        "fr": "Impossible de lire le script : %1",
        "es": "No se pudo leer el script: %1",
        "pt_BR": "Não foi possível ler o script: %1",
        "ru": "Не удалось прочитать скрипт: %1",
    },
    "Close Connection": {
        "zh_CN": "关闭连接", "ja": "接続を閉じる", "ko": "연결 닫기",
        "de": "Verbindung schließen", "fr": "Fermer la connexion",
        "es": "Cerrar la conexión", "pt_BR": "Fechar a conexão", "ru": "Закрыть подключение",
    },
    "New In-Memory Connection": {
        "zh_CN": "新建内存连接", "ja": "新規インメモリ接続", "ko": "새 인메모리 연결",
        "de": "Neue In-Memory-Verbindung", "fr": "Nouvelle connexion en mémoire",
        "es": "Nueva conexión en memoria", "pt_BR": "Nova conexão em memória",
        "ru": "Новое подключение в памяти",
    },
    "Run": {
        "zh_CN": "执行", "ja": "実行", "ko": "실행", "de": "Ausführen",
        "fr": "Exécuter", "es": "Ejecutar", "pt_BR": "Executar", "ru": "Запустить",
    },
    "Run the SQL (Ctrl+⏎)": {
        "zh_CN": "执行 SQL (Ctrl+⏎)", "ja": "SQL を実行 (Ctrl+⏎)", "ko": "SQL 실행 (Ctrl+⏎)",
        "de": "SQL ausführen (Ctrl+⏎)", "fr": "Exécuter le SQL (Ctrl+⏎)",
        "es": "Ejecutar el SQL (Ctrl+⏎)", "pt_BR": "Executar o SQL (Ctrl+⏎)",
        "ru": "Запустить SQL (Ctrl+⏎)",
    },
    "Close Result Tab": {
        "zh_CN": "关闭结果页签", "ja": "結果タブを閉じる", "ko": "결과 탭 닫기",
        "de": "Ergebnis-Tab schließen", "fr": "Fermer l'onglet de résultats",
        "es": "Cerrar la pestaña de resultados", "pt_BR": "Fechar a aba de resultados",
        "ru": "Закрыть вкладку результатов",
    },
    "&Query": {
        "zh_CN": "查询(&Q)", "ja": "クエリ(&Q)", "ko": "쿼리(&Q)", "de": "&Abfrage",
        "fr": "&Requête", "es": "&Consulta", "pt_BR": "&Consulta", "ru": "&Запрос",
    },
    "&Run SQL": {
        "zh_CN": "执行 SQL(&R)", "ja": "SQL を実行(&R)", "ko": "SQL 실행(&R)",
        "de": "SQL &ausführen", "fr": "Exécuter le &SQL", "es": "&Ejecutar SQL",
        "pt_BR": "&Executar SQL", "ru": "&Запустить SQL",
    },
    "&View": {
        "zh_CN": "视图(&V)", "ja": "表示(&V)", "ko": "보기(&V)", "de": "&Ansicht",
        "fr": "&Affichage", "es": "&Ver", "pt_BR": "&Ver", "ru": "&Вид",
    },
    "&Theme": {
        "zh_CN": "主题(&T)", "ja": "テーマ(&T)", "ko": "테마(&T)", "de": "&Design",
        "fr": "&Thème", "es": "&Tema", "pt_BR": "&Tema", "ru": "&Тема",
    },
    "Follow System": {
        "zh_CN": "跟随系统", "ja": "システムに従う", "ko": "시스템 따르기", "de": "System folgen",
        "fr": "Suivre le système", "es": "Seguir el sistema", "pt_BR": "Seguir o sistema",
        "ru": "Как в системе",
    },
    "Light": {
        "zh_CN": "浅色", "ja": "ライト", "ko": "라이트", "de": "Hell",
        "fr": "Clair", "es": "Claro", "pt_BR": "Claro", "ru": "Светлая",
    },
    "Dark": {
        "zh_CN": "深色", "ja": "ダーク", "ko": "다크", "de": "Dunkel",
        "fr": "Sombre", "es": "Oscuro", "pt_BR": "Escuro", "ru": "Тёмная",
    },
    "%1 already exists. Open it as an existing database?": {
        "zh_CN": "%1 已存在。作为现有数据库打开？",
        "ja": "%1 は既に存在します。既存のデータベースとして開きますか？",
        "ko": "%1이(가) 이미 있습니다. 기존 데이터베이스로 열까요?",
        "de": "%1 existiert bereits. Als vorhandene Datenbank öffnen?",
        "fr": "%1 existe déjà. L'ouvrir comme base de données existante ?",
        "es": "%1 ya existe. ¿Abrirla como base de datos existente?",
        "pt_BR": "%1 já existe. Abrir como banco de dados existente?",
        "ru": "%1 уже существует. Открыть как существующую базу данных?",
    },
    "Running — %1": {
        "zh_CN": "执行中 — %1", "ja": "実行中 — %1", "ko": "실행 중 — %1", "de": "Läuft — %1",
        "fr": "En cours — %1", "es": "Ejecutando — %1", "pt_BR": "Em execução — %1",
        "ru": "Выполняется — %1",
    },
    "Cancel the running query on %1": {
        "zh_CN": "取消 %1 上正在执行的查询",
        "ja": "%1 で実行中のクエリをキャンセル",
        "ko": "%1에서 실행 중인 쿼리 취소",
        "de": "Laufende Abfrage auf %1 abbrechen",
        "fr": "Annuler la requête en cours sur %1",
        "es": "Cancelar la consulta en curso en %1",
        "pt_BR": "Cancelar a consulta em andamento em %1",
        "ru": "Отменить выполняемый запрос на %1",
    },
    "Copy Cell": {
        "zh_CN": "复制单元格", "ja": "セルをコピー", "ko": "셀 복사", "de": "Zelle kopieren",
        "fr": "Copier la cellule", "es": "Copiar celda", "pt_BR": "Copiar célula",
        "ru": "Копировать ячейку",
    },
    "Copy Row": {
        "zh_CN": "复制行", "ja": "行をコピー", "ko": "행 복사", "de": "Zeile kopieren",
        "fr": "Copier la ligne", "es": "Copiar fila", "pt_BR": "Copiar linha",
        "ru": "Копировать строку",
    },
    "Copy Selection": {
        "zh_CN": "复制所选", "ja": "選択範囲をコピー", "ko": "선택 영역 복사", "de": "Auswahl kopieren",
        "fr": "Copier la sélection", "es": "Copiar selección", "pt_BR": "Copiar seleção",
        "ru": "Копировать выделенное",
    },
    "Copy Selection with Headers": {
        "zh_CN": "复制所选（含表头）",
        "ja": "選択範囲をヘッダー付きでコピー",
        "ko": "선택 영역 복사(헤더 포함)",
        "de": "Auswahl mit Kopfzeilen kopieren",
        "fr": "Copier la sélection avec les en-têtes",
        "es": "Copiar selección con encabezados",
        "pt_BR": "Copiar seleção com cabeçalhos",
        "ru": "Копировать выделенное с заголовками",
    },
    "No matches": {
        "zh_CN": "无匹配", "ja": "該当なし", "ko": "일치 항목 없음", "de": "Keine Treffer",
        "fr": "Aucun résultat", "es": "Sin coincidencias", "pt_BR": "Nenhuma correspondência",
        "ru": "Нет совпадений",
    },
    "%1 matches": {
        "zh_CN": "%1 个匹配", "ja": "%1 件一致", "ko": "일치 항목 %1개", "de": "%1 Treffer",
        "fr": "%1 résultats", "es": "%1 coincidencias", "pt_BR": "%1 correspondências",
        "ru": "Совпадений: %1",
    },
    "Find": {
        "zh_CN": "查找", "ja": "検索", "ko": "찾기", "de": "Suchen",
        "fr": "Rechercher", "es": "Buscar", "pt_BR": "Localizar", "ru": "Найти",
    },
    "Replace with": {
        "zh_CN": "替换为", "ja": "置換後の文字列", "ko": "바꿀 내용", "de": "Ersetzen durch",
        "fr": "Remplacer par", "es": "Reemplazar por", "pt_BR": "Substituir por",
        "ru": "Заменить на",
    },
    "Previous match": {
        "zh_CN": "上一个匹配", "ja": "前の一致項目", "ko": "이전 일치 항목", "de": "Vorheriger Treffer",
        "fr": "Résultat précédent", "es": "Coincidencia anterior", "pt_BR": "Correspondência anterior",
        "ru": "Предыдущее совпадение",
    },
    "Next match": {
        "zh_CN": "下一个匹配", "ja": "次の一致項目", "ko": "다음 일치 항목", "de": "Nächster Treffer",
        "fr": "Résultat suivant", "es": "Siguiente coincidencia", "pt_BR": "Próxima correspondência",
        "ru": "Следующее совпадение",
    },
    "Replace": {
        "zh_CN": "替换", "ja": "置換", "ko": "바꾸기", "de": "Ersetzen",
        "fr": "Remplacer", "es": "Reemplazar", "pt_BR": "Substituir", "ru": "Заменить",
    },
    "Replace All": {
        "zh_CN": "全部替换", "ja": "すべて置換", "ko": "모두 바꾸기", "de": "Alle ersetzen",
        "fr": "Tout remplacer", "es": "Reemplazar todo", "pt_BR": "Substituir tudo",
        "ru": "Заменить все",
    },
    "Close": {
        "zh_CN": "关闭", "ja": "閉じる", "ko": "닫기", "de": "Schließen",
        "fr": "Fermer", "es": "Cerrar", "pt_BR": "Fechar", "ru": "Закрыть",
    },
    "Filter tables…": {
        "zh_CN": "筛选表…", "ja": "テーブルを絞り込み…", "ko": "테이블 필터…",
        "de": "Tabellen filtern…", "fr": "Filtrer les tables…", "es": "Filtrar tablas…",
        "pt_BR": "Filtrar tabelas…", "ru": "Фильтр таблиц…",
    },
    "Active connection (Ctrl+Tab cycles)": {
        "zh_CN": "当前连接（Ctrl+Tab 切换）",
        "ja": "アクティブな接続（Ctrl+Tab で切り替え）",
        "ko": "활성 연결(Ctrl+Tab으로 전환)",
        "de": "Aktive Verbindung (Ctrl+Tab wechselt)",
        "fr": "Connexion active (Ctrl+Tab pour naviguer)",
        "es": "Conexión activa (Ctrl+Tab cambia)",
        "pt_BR": "Conexão ativa (Ctrl+Tab alterna)",
        "ru": "Активное подключение (Ctrl+Tab — переключение)",
    },
    "Ln %1, Col %2": {
        "zh_CN": "第 %1 行，第 %2 列", "ja": "%1 行 %2 列", "ko": "%1행 %2열",
        "de": "Z %1, Sp %2", "fr": "Lig %1, Col %2", "es": "Lín %1, Col %2",
        "pt_BR": "Lin %1, Col %2", "ru": "Стр %1, стлб %2",
    },
    "Run a query (Ctrl+⏎) to see results here": {
        "zh_CN": "执行查询（Ctrl+⏎）后结果将显示在这里",
        "ja": "クエリを実行（Ctrl+⏎）すると結果がここに表示されます",
        "ko": "쿼리를 실행(Ctrl+⏎)하면 여기에 결과가 표시됩니다",
        "de": "Abfrage ausführen (Ctrl+⏎), um Ergebnisse hier zu sehen",
        "fr": "Exécutez une requête (Ctrl+⏎) pour voir les résultats ici",
        "es": "Ejecuta una consulta (Ctrl+⏎) para ver los resultados aquí",
        "pt_BR": "Execute uma consulta (Ctrl+⏎) para ver os resultados aqui",
        "ru": "Выполните запрос (Ctrl+⏎), и результаты появятся здесь",
    },
    "Cursor position": {
        "zh_CN": "光标位置", "ja": "カーソル位置", "ko": "커서 위치", "de": "Cursorposition",
        "fr": "Position du curseur", "es": "Posición del cursor", "pt_BR": "Posição do cursor",
        "ru": "Позиция курсора",
    },
    "Ln %L1, Col %L2": {
        "zh_CN": "第 %L1 行，第 %L2 列", "ja": "%L1 行 %L2 列", "ko": "%L1행 %L2열",
        "de": "Z %L1, Sp %L2", "fr": "Lig %L1, Col %L2", "es": "Lín %L1, Col %L2",
        "pt_BR": "Lin %L1, Col %L2", "ru": "Стр %L1, стлб %L2",
    },
    "Connect to DuckDB Server…": {
        "zh_CN": "连接到 DuckDB 服务器…", "ja": "DuckDB サーバーに接続…", "ko": "DuckDB 서버에 연결…",
        "de": "Mit DuckDB-Server verbinden…", "fr": "Se connecter au serveur DuckDB…",
        "es": "Conectar con el servidor DuckDB…", "pt_BR": "Conectar ao servidor DuckDB…",
        "ru": "Подключиться к серверу DuckDB…",
    },
    "Connect to DuckDB Server": {
        "zh_CN": "连接到 DuckDB 服务器", "ja": "DuckDB サーバーに接続", "ko": "DuckDB 서버에 연결",
        "de": "Mit DuckDB-Server verbinden", "fr": "Connexion au serveur DuckDB",
        "es": "Conexión al servidor DuckDB", "pt_BR": "Conectar ao servidor DuckDB",
        "ru": "Подключение к серверу DuckDB",
    },
    "Connect": {
        "zh_CN": "连接", "ja": "接続", "ko": "연결",
        "de": "Verbinden", "fr": "Se connecter", "es": "Conectar",
        "pt_BR": "Conectar", "ru": "Подключиться",
    },
    "Cancel": {
        "zh_CN": "取消", "ja": "キャンセル", "ko": "취소",
        "de": "Abbrechen", "fr": "Annuler", "es": "Cancelar",
        "pt_BR": "Cancelar", "ru": "Отмена",
    },
    "&Server address:": {
        "zh_CN": "服务器地址(&S)：", "ja": "サーバーアドレス(&S):", "ko": "서버 주소(&S):",
        "de": "&Serveradresse:", "fr": "&Adresse du serveur :",
        "es": "Dirección del &servidor:", "pt_BR": "Endereço do &servidor:",
        "ru": "Адрес &сервера:",
    },
    "&Token (optional):": {
        "zh_CN": "令牌（可选）(&T)：", "ja": "トークン（省略可）(&T):", "ko": "토큰(선택 사항)(&T):",
        "de": "&Token (optional):", "fr": "&Jeton (facultatif) :",
        "es": "&Token (opcional):", "pt_BR": "&Token (opcional):",
        "ru": "&Токен (необязательно):",
    },
    "&Alias:": {
        "zh_CN": "别名(&A)：", "ja": "エイリアス(&A):", "ko": "별칭(&A):",
        "de": "&Alias:", "fr": "&Alias :", "es": "&Alias:",
        "pt_BR": "&Alias:", "ru": "&Псевдоним:",
    },
    "Leave empty if the server has no token": {
        "zh_CN": "服务器没有令牌时留空",
        "ja": "サーバーにトークンがない場合は空のままにしてください",
        "ko": "서버에 토큰이 없으면 비워 두세요",
        "de": "Leer lassen, wenn der Server keinen Token hat",
        "fr": "Laisser vide si le serveur n'a pas de jeton",
        "es": "Dejar vacío si el servidor no tiene token",
        "pt_BR": "Deixe vazio se o servidor não tiver token",
        "ru": "Оставьте пустым, если у сервера нет токена",
    },
    "Name the remote database gets after ATTACH": {
        "zh_CN": "远程数据库 ATTACH 后使用的名称",
        "ja": "ATTACH 後のリモートデータベースの名前",
        "ko": "ATTACH 후 원격 데이터베이스의 이름",
        "de": "Name, den die entfernte Datenbank nach dem ATTACH erhält",
        "fr": "Nom que la base de données distante reçoit après ATTACH",
        "es": "Nombre que recibe la base de datos remota tras ATTACH",
        "pt_BR": "Nome que o banco de dados remoto recebe após o ATTACH",
        "ru": "Имя, которое получит удалённая база данных после ATTACH",
    },
    "Enter a server address, e.g. localhost:9494.": {
        "zh_CN": "请输入服务器地址，例如 localhost:9494。",
        "ja": "サーバーアドレスを入力してください（例: localhost:9494）。",
        "ko": "서버 주소를 입력하세요(예: localhost:9494).",
        "de": "Bitte eine Serveradresse eingeben, z. B. localhost:9494.",
        "fr": "Saisissez une adresse de serveur, p. ex. localhost:9494.",
        "es": "Introduzca una dirección de servidor, p. ej. localhost:9494.",
        "pt_BR": "Informe um endereço de servidor, por exemplo localhost:9494.",
        "ru": "Укажите адрес сервера, например localhost:9494.",
    },
    "The alias must be a plain identifier (letters, digits, underscore).": {
        "zh_CN": "别名只能是普通标识符（字母、数字、下划线）。",
        "ja": "エイリアスには普通の識別子（英数字とアンダースコア）のみ使えます。",
        "ko": "별칭에는 일반 식별자(영문자, 숫자, 밑줄)만 사용할 수 있습니다.",
        "de": "Der Alias muss ein einfacher Bezeichner sein (Buchstaben, Ziffern, Unterstrich).",
        "fr": "L'alias doit être un identifiant simple (lettres, chiffres, tiret bas).",
        "es": "El alias debe ser un identificador simple (letras, dígitos, guion bajo).",
        "pt_BR": "O alias deve ser um identificador simples (letras, dígitos e sublinhado).",
        "ru": "Псевдоним должен быть простым идентификатором (буквы, цифры, подчёркивание).",
    },
    "Already connected: %1": {
        "zh_CN": "已连接：%1", "ja": "すでに接続中：%1", "ko": "이미 연결됨: %1",
        "de": "Bereits verbunden: %1", "fr": "Déjà connecté : %1",
        "es": "Ya conectado: %1", "pt_BR": "Já conectado: %1",
        "ru": "Подключение уже есть: %1",
    },
    "Connected to %1": {
        "zh_CN": "已连接到 %1", "ja": "%1 に接続しました", "ko": "%1에 연결됨",
        "de": "Verbunden mit %1", "fr": "Connecté à %1",
        "es": "Conectado a %1", "pt_BR": "Conectado a %1",
        "ru": "Подключено к %1",
    },
}

LANGS = ["zh_CN", "ja", "ko", "de", "fr", "es", "pt_BR", "ru"]

def main() -> int:
    missing = set()
    for path in sorted(glob.glob("translations/ducq_*.ts")):
        lang = path.split("ducq_")[1].split(".")[0]
        tree = ET.parse(path)
        root = tree.getroot()
        count = 0
        for message in root.iter("message"):
            source = message.find("source").text
            if source is None:
                continue
            table = T.get(source)
            if table is None:
                missing.add(source)
                continue
            translation = table.get(lang, source)
            node = message.find("translation")
            node.text = translation
            if node.attrib.get("type") == "unfinished":
                del node.attrib["type"]
            count += 1
        tree.write(path, encoding="utf-8", xml_declaration=True)
        print(f"{path}: {count} messages updated")
    if missing:
        print("MISSING translations for:", file=sys.stderr)
        for s in sorted(missing):
            print("  " + repr(s), file=sys.stderr)
        return 1
    return 0

if __name__ == "__main__":
    sys.exit(main())
